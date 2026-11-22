#include "honda_v6.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include "../encode_plugin.h"

#define TAG "HondaV6"

#define HV6_TS      100
#define HV6_TL      200
#define HV6_TD      45
#define HV6_MIN_BITS 43
#define HV6_MIN_PRE 20
#define HV6_PAN_MIN 118
#define HV6_PAN_MAX 140
#define HV6_PAN_HALFA_MIN 45
#define HV6_PAN_HALFA_MAX 65
#define HV6_PAN_HALFB 78
#define HV6_PAN_MIDGAP_MIN 1000
#define HV6_PAN_MIDGAP_MAX 2000

#define HV6_SHORT_LOW  0
#define HV6_SHORT_HIGH 2
#define HV6_LONG_LOW   4
#define HV6_LONG_HIGH  6
#define HV6_RESET_EV   8

#define HV6_T313_S       800
#define HV6_T313_M       1200
#define HV6_T313_L       1600
#define HV6_T313_D       150
#define HV6_T313_MINPRE  15
#define HV6_T313_MINTRIT 30
#define HV6_T313_MAXTRIT 60

static const SubGhzBlockConst subghz_protocol_honda_v6_const = {
    .te_short = HV6_TS,
    .te_long = HV6_TL,
    .te_delta = HV6_TD,
    .min_count_bit_for_found = 45,
};

static const uint8_t hv6_man_table[4] = {0x01, 0x91, 0x9B, 0xFB};

struct SubGhzProtocolDecoderHondaV6 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t parser_step;
    uint32_t header;
    uint8_t man_state;
    uint64_t lo;
    uint64_t hi;
    uint16_t bit_count;
    bool midgap;
    uint16_t halfa;

    uint32_t serial;
    uint8_t btn;
    uint32_t cnt;
    uint8_t variant;
    uint64_t data_hi;

    uint16_t trit_count;
    uint64_t d2;
    uint64_t d1;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderHondaV6);

typedef enum {
    HondaV6StepReset = 0,
    HondaV6StepCheckPre = 1,
    HondaV6StepCollect = 2,
    HondaV6StepPre313 = 3,
    HondaV6StepData313 = 4,
} HondaV6Step;

static int hv6_man_advance(uint8_t* state, uint8_t event) {
    if(event == HV6_RESET_EV) {
        *state = 1;
        return -1;
    }
    uint8_t ns = (hv6_man_table[*state] >> event) & 0x3;
    if(ns == *state) {
        *state = 1;
        return -1;
    }
    *state = ns;
    if(ns == 2) return 0;
    if(ns == 1) return 1;
    return -1;
}

static void hv6_man_reset(SubGhzProtocolDecoderHondaV6* instance) {
    instance->man_state = 1;
}

static int hv6_man_adv(SubGhzProtocolDecoderHondaV6* instance, bool level, uint32_t duration) {
    uint8_t ev;
    if(DURATION_DIFF(duration, HV6_TS) <= HV6_TD) {
        ev = level ? HV6_SHORT_HIGH : HV6_SHORT_LOW;
    } else if(DURATION_DIFF(duration, HV6_TL) <= HV6_TD) {
        ev = level ? HV6_LONG_HIGH : HV6_LONG_LOW;
    } else {
        return -1;
    }
    return hv6_man_advance(&instance->man_state, ev);
}

static void hv6_append(SubGhzProtocolDecoderHondaV6* instance, int bit) {
    if(instance->bit_count < 128) {
        instance->hi = (instance->hi << 1) | (instance->lo >> 63);
        instance->lo = (instance->lo << 1) | (uint64_t)(bit ? 1 : 0);
    }
    instance->bit_count++;
}

static bool hv6_skeleton_ok(uint64_t hi, uint64_t lo) {
    return ((lo >> 40) & 0xFFFFFFULL) == 0x1E1E1FULL && (lo & 0xFFULL) == 0x51ULL &&
           (hi & 0xFFULL) == 0xFEULL;
}

static bool hv6_undrift(uint64_t hi, uint64_t lo, uint16_t bit_count, uint64_t* nhi, uint64_t* nlo) {
    if(bit_count >= 135 || bit_count < HV6_PAN_MIN) return false;
    int shift = 135 - bit_count;
    if(shift > 16) return false;
    uint64_t new_lo = (lo >> shift) | (hi << (64 - shift));
    uint64_t new_hi = hi >> shift;
    if(hv6_skeleton_ok(new_hi, new_lo)) {
        *nhi = new_hi;
        *nlo = new_lo;
        return true;
    }
    return false;
}

static uint8_t hv6_identify_btn(uint64_t hop) {
    uint8_t b34 = (hop >> 34) & 1;
    uint8_t b31 = (hop >> 31) & 1;
    uint8_t b29 = (hop >> 29) & 1;
    if(b34) return 0x02;
    if(b29) return 0x01;
    if(b31) return 0x10;
    return 0x03;
}

static uint16_t hv6_pan_cnt_width(uint16_t bc) {
    if(bc < 119) return 0;
    uint16_t w = bc - 119;
    return w > 16 ? 16 : w;
}

static uint32_t hv6_pan_counter(uint64_t hi, uint16_t bc) {
    uint16_t w = hv6_pan_cnt_width(bc);
    if(w == 0) return 0;
    uint16_t n = bc >= 128 ? 128 : bc;
    int shift = (int)n - 64 - (int)w;
    if(shift < 0) return 0;
    return (uint32_t)((hi >> shift) & (((uint64_t)1 << w) - 1));
}

static void hv6_emit(
    SubGhzProtocolDecoderHondaV6* instance,
    uint8_t variant,
    uint32_t serial,
    uint8_t btn,
    uint32_t cnt,
    uint64_t data_lo,
    uint64_t data_hi,
    uint16_t bits) {
    instance->variant = variant;
    instance->serial = serial;
    instance->btn = btn;
    instance->cnt = cnt;
    instance->data_hi = data_hi;

    instance->generic.data = data_lo;
    instance->generic.data_count_bit = bits;
    instance->generic.serial = serial;
    instance->generic.btn = btn;
    instance->generic.cnt = cnt;
    instance->decoder.decode_data = data_lo;
    instance->decoder.decode_count_bit = bits;

    if(instance->base.callback) instance->base.callback(&instance->base, instance->base.context);
}

static bool hv6_serial_degenerate(uint32_t serial);

static void hv6_process_fsk(SubGhzProtocolDecoderHondaV6* instance) {
    uint16_t bc = instance->bit_count;

    bool is_stock = (bc == 45);
    bool is_pan = (HV6_PAN_MIN <= bc && bc <= HV6_PAN_MAX);
    if(!is_stock && !is_pan) return;

    uint64_t hi = instance->hi, lo = instance->lo;
    if(is_pan) {
        if(bc < 135) {
            uint64_t nhi, nlo;
            if(!hv6_undrift(hi, lo, bc, &nhi, &nlo)) return;
            hi = nhi;
            lo = nlo;
            bc = 135;
        } else if(!hv6_skeleton_ok(hi, lo)) {
            return;
        }
    }

    if(is_stock) {
        uint64_t norm = lo & 0x1FFFFFFFFFFFULL;
        uint32_t cnt = (uint32_t)((norm >> 41) & 0xF);
        uint64_t hop = norm & 0x1FFFFFFFFFFULL;
        uint8_t btn = hv6_identify_btn(hop);
        uint32_t serial = (uint32_t)(hop >> 9);
        if(serial == 0) return;

        if(hv6_serial_degenerate(serial)) return;
        hv6_emit(instance, 0, serial, btn, cnt, norm, 0, 45);
    } else {
        uint32_t serial = (uint32_t)((lo >> 32) & 0xFFFFFFFFULL);
        uint32_t cnt = hv6_pan_counter(hi, bc);
        hv6_emit(instance, 1, serial, 0, cnt, lo, hi, bc);
    }
}

static uint8_t hv6_classify313(uint32_t duration) {
    if(DURATION_DIFF(duration, HV6_T313_S) <= HV6_T313_D) return 0;
    if(DURATION_DIFF(duration, HV6_T313_M) <= HV6_T313_D) return 1;
    if(DURATION_DIFF(duration, HV6_T313_L) <= HV6_T313_D) return 2;
    return 3;
}

static void hv6_add_trit(SubGhzProtocolDecoderHondaV6* instance, uint8_t trit) {
    if(instance->trit_count >= HV6_T313_MAXTRIT) return;
    uint16_t idx = instance->trit_count;
    if(idx < 32) {
        instance->d2 |= (uint64_t)(trit & 3) << (62 - idx * 2);
    } else if(idx < 64) {
        instance->d1 |= (uint64_t)(trit & 3) << (62 - (idx - 32) * 2);
    }
    instance->trit_count++;
}

static bool hv6_serial_degenerate(uint32_t serial) {
    uint8_t counts[16] = {0};
    for(uint8_t i = 0U; i < 8U; i++) counts[(serial >> (4U * i)) & 0x0FU]++;
    for(uint8_t n = 0U; n < 16U; n++)
        if(counts[n] >= 6U) return true;
    return false;
}

static void hv6_process313(SubGhzProtocolDecoderHondaV6* instance) {
    if(instance->trit_count < HV6_T313_MINTRIT) return;
    uint32_t serial = (uint32_t)((instance->d2 >> 32) & 0xFFFFFFFFULL);
    if(serial == 0) return;
    if(hv6_serial_degenerate(serial)) return;

    {
        uint16_t tcnt[4] = {0, 0, 0, 0};
        for(uint16_t i = 0U; i < instance->trit_count; i++) {
            uint64_t word = (i < 32U) ? instance->d2 : instance->d1;
            uint16_t idx = (i < 32U) ? i : (uint16_t)(i - 32U);
            uint8_t tv = (uint8_t)((word >> (62 - idx * 2)) & 0x3U);
            tcnt[tv]++;
        }
        if(tcnt[0] < 2U || tcnt[1] < 2U || tcnt[2] < 2U) return;
    }

    hv6_emit(
        instance,
        2,
        serial,
        0,
        instance->trit_count,
        instance->d2,
        instance->d1,
        (uint16_t)(instance->trit_count * 2));
}

void* subghz_protocol_decoder_honda_v6_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderHondaV6* instance = calloc(1, sizeof(SubGhzProtocolDecoderHondaV6));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_honda_v6;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->man_state = 1;
    return instance;
}

void subghz_protocol_decoder_honda_v6_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV6* instance = context;
    instance->parser_step = HondaV6StepReset;
    instance->header = 0;
    instance->man_state = 1;
    instance->lo = 0;
    instance->hi = 0;
    instance->bit_count = 0;
    instance->midgap = false;
    instance->halfa = 0;
    instance->trit_count = 0;
    instance->d2 = 0;
    instance->d1 = 0;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_honda_v6_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV6* instance = context;

    bool is_s = DURATION_DIFF(duration, HV6_TS) <= HV6_TD;
    bool is_l = DURATION_DIFF(duration, HV6_TL) <= HV6_TD;
    bool is_s313 = DURATION_DIFF(duration, HV6_T313_S) <= HV6_T313_D;
    uint8_t trit313 = hv6_classify313(duration);

    switch(instance->parser_step) {
    case HondaV6StepReset:
        if(level && is_s) {
            instance->parser_step = HondaV6StepCheckPre;
            instance->header = 0;
            instance->lo = 0;
            instance->hi = 0;
            instance->bit_count = 0;
            instance->midgap = false;
            hv6_man_reset(instance);
        }

        if(is_s313 && !is_s) {
            instance->parser_step = HondaV6StepPre313;
            instance->header = 1;
        }
        break;

    case HondaV6StepCheckPre:
        if(is_s) {
            instance->header++;
        } else if(is_l && instance->header >= HV6_MIN_PRE) {
            instance->parser_step = HondaV6StepCollect;
            instance->lo = 0;
            instance->hi = 0;
            instance->bit_count = 0;
            instance->midgap = false;
            instance->halfa = 0;
            hv6_man_reset(instance);
            uint8_t ev = level ? HV6_LONG_HIGH : HV6_LONG_LOW;
            int bit = hv6_man_advance(&instance->man_state, ev);
            if(bit >= 0) hv6_append(instance, bit);
        } else if(!is_s) {
            instance->parser_step = HondaV6StepReset;
        }
        break;

    case HondaV6StepCollect:
        if(is_s || is_l) {
            int bit = hv6_man_adv(instance, level, duration);
            if(bit >= 0) hv6_append(instance, bit);
            if(instance->midgap && instance->halfa > 0 &&
               instance->bit_count >= instance->halfa + HV6_PAN_HALFB) {
                hv6_process_fsk(instance);
                instance->parser_step = HondaV6StepReset;
            }
        } else {
            bool is_midgap = (!level) && HV6_PAN_MIDGAP_MIN <= duration &&
                             duration <= HV6_PAN_MIDGAP_MAX;
            if(is_midgap && !instance->midgap && HV6_PAN_HALFA_MIN <= instance->bit_count &&
               instance->bit_count <= HV6_PAN_HALFA_MAX) {
                instance->midgap = true;
                instance->halfa = instance->bit_count;
                hv6_man_reset(instance);
                return;
            }
            if(!level) {
                int bit = hv6_man_advance(&instance->man_state, HV6_LONG_LOW);
                if(bit >= 0) hv6_append(instance, bit);
            }
            hv6_process_fsk(instance);
            instance->parser_step = HondaV6StepReset;
        }
        break;

    case HondaV6StepPre313:
        if(is_s313) {
            instance->header++;
        } else if(instance->header >= HV6_T313_MINPRE && trit313 <= 2) {
            instance->parser_step = HondaV6StepData313;
            instance->trit_count = 0;
            instance->d1 = 0;
            instance->d2 = 0;
            hv6_add_trit(instance, trit313);
        } else {
            instance->parser_step = HondaV6StepReset;
        }
        break;

    case HondaV6StepData313:
        if(trit313 <= 2) {
            hv6_add_trit(instance, trit313);
        } else {
            hv6_process313(instance);
            instance->parser_step = HondaV6StepReset;
        }
        break;

    default:
        break;
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_honda_v6_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV6* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t serial = instance->serial;
    uint32_t btn = instance->btn;
    uint32_t cnt = instance->cnt;
    uint32_t variant = instance->variant;
    if(!flipper_format_write_uint32(flipper_format, "Serial", &serial, 1) ||
       !flipper_format_write_uint32(flipper_format, "Btn", &btn, 1) ||
       !flipper_format_write_uint32(flipper_format, "Cnt", &cnt, 1) ||
       !flipper_format_write_uint32(flipper_format, "Variant", &variant, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    if(instance->variant == 1) {

        uint16_t dcb = instance->generic.data_count_bit;
        if(dcb >= 128 && dcb <= 144) {
            uint16_t lead = (uint16_t)(dcb - 128);
            uint8_t fr[18];
            memset(fr, 0, sizeof(fr));
            uint64_t hi = instance->data_hi;
            uint64_t lo = instance->generic.data;
            for(int b = 0; b < 64; b++)
                if((hi >> (63 - b)) & 1) fr[(lead + b) >> 3] |= (uint8_t)(0x80U >> ((lead + b) & 7));
            for(int b = 0; b < 64; b++)
                if((lo >> (63 - b)) & 1)
                    fr[(lead + 64 + b) >> 3] |= (uint8_t)(0x80U >> ((lead + 64 + b) & 7));
            if(!flipper_format_write_hex(flipper_format, "Data", fr, (dcb + 7) / 8)) {
                return SubGhzProtocolStatusErrorParserOthers;
            }
        }
    } else if(instance->variant == 2) {

        uint8_t hib[8];
        for(int b = 0; b < 8; b++) hib[b] = (uint8_t)(instance->data_hi >> (56 - b * 8));
        if(!flipper_format_write_hex(flipper_format, "DataHi", hib, 8)) {
            return SubGhzProtocolStatusErrorParserOthers;
        }
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_honda_v6_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV6* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_honda_v6_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;

    uint32_t tmp = 0;
    flipper_format_rewind(flipper_format);
    instance->serial = flipper_format_read_uint32(flipper_format, "Serial", &tmp, 1) ? tmp : 0;
    flipper_format_rewind(flipper_format);
    instance->btn = flipper_format_read_uint32(flipper_format, "Btn", &tmp, 1) ? (uint8_t)tmp : 0;
    flipper_format_rewind(flipper_format);
    instance->cnt = flipper_format_read_uint32(flipper_format, "Cnt", &tmp, 1) ? tmp : 0;
    flipper_format_rewind(flipper_format);
    instance->variant =
        flipper_format_read_uint32(flipper_format, "Variant", &tmp, 1) ? (uint8_t)tmp : 0;

    instance->generic.serial = instance->serial;
    instance->generic.btn = instance->btn;
    instance->generic.cnt = instance->cnt;
    instance->decoder.decode_data = instance->generic.data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

const SubGhzProtocolDecoder subghz_protocol_honda_v6_decoder = {
    .alloc = subghz_protocol_decoder_honda_v6_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_honda_v6_feed,
    .reset = subghz_protocol_decoder_honda_v6_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_honda_v6_serialize,
    .deserialize = subghz_protocol_decoder_honda_v6_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_honda_v6_encoder_ext =
    SUBGHZ_ENCODER_EXTERNAL("enc_recon_manchester");

const SubGhzProtocol subghz_protocol_honda_v6 = {
    .name = HONDA_V6_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 |
            SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_honda_v6_decoder,
    .encoder = &subghz_protocol_honda_v6_encoder_ext.vtable,
};
