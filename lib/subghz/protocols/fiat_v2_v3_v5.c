

#include "fiat_v2_v3_v5.h"

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#include <lib/subghz/blocks/custom_btn_i.h>
#include "common.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"

#define TAG "SubGhzProtocolFiat"

#define FIAT_V3_TE_SHORT         460
#define FIAT_V3_TE_LONG          920
#define FIAT_V3_TE_DELTA         200
#define FIAT_V3_MIN_GAP          1500
#define FIAT_V3_FRAME_BITS       70
#define FIAT_V3_MIN_FRAME_BITS   60
#define FIAT_V3_MAX_BITS         100
#define FIAT_V3_PREAMBLE_MIN     4
#define FIAT_V3_PREAMBLE_MAX     20
#define FIAT_V3_PREAMBLE_BITS    13
#define FIAT_V3_MAX_TIMINGS      170
#define FIAT_V3_MIN_PREAMBLE_GAP 400

static const SubGhzBlockConst subghz_protocol_fiat_v3_const = {
    .te_short = FIAT_V3_TE_SHORT,
    .te_long = FIAT_V3_TE_LONG,
    .te_delta = FIAT_V3_TE_DELTA,
    .min_count_bit_for_found = FIAT_V3_MIN_FRAME_BITS,
};

#define FIAT_V5_TE_SHORT       200
#define FIAT_V5_TE_LONG        400
#define FIAT_V5_TE_DELTA       100
#define FIAT_V5_MIN_GAP        2000
#define FIAT_V5_FRAME_BITS     97
#define FIAT_V5_MIN_FRAME_BITS 90
#define FIAT_V5_MAX_BITS       130
#define FIAT_V5_PREAMBLE_MIN   10
#define FIAT_V5_PREAMBLE_BITS  15

static const SubGhzBlockConst subghz_protocol_fiat_v5_const = {
    .te_short = FIAT_V5_TE_SHORT,
    .te_long = FIAT_V5_TE_LONG,
    .te_delta = FIAT_V5_TE_DELTA,
    .min_count_bit_for_found = FIAT_V5_MIN_FRAME_BITS,
};

static const uint8_t fiat_manchester_table[4] = {0x01, 0x91, 0x9B, 0xFB};

static bool fiat_manchester_decode(uint8_t* state, uint8_t timing_type, uint8_t* bit_out) {
    if(timing_type == 8) {
        *state = 1;
        return false;
    }
    if(*state >= 4) {
        *state = 1;
        return false;
    }

    uint8_t entry = fiat_manchester_table[*state];
    uint8_t new_state = (entry >> timing_type) & 0x3;

    if(*state == new_state) {
        return false;
    }

    *state = new_state;

    if(new_state == 2) {
        if(bit_out) *bit_out = 0;
        return true;
    } else if(new_state == 1) {
        if(bit_out) *bit_out = 1;
        return true;
    }

    return false;
}

typedef enum {
    FiatVariantV3 = 3,
    FiatVariantV5 = 5,
} FiatVariant;

#define FIAT_VARIANT_IDX(v) ((v) == FiatVariantV3 ? 0 : 1)
#define FIAT_VARIANT_COUNT  2

typedef struct {
    FiatVariant variant;
    const SubGhzBlockConst* block_const;
    uint32_t min_gap;
    uint16_t frame_bits;
    uint16_t max_bits;
    uint8_t preamble_bits;
    uint8_t btn_max;
    bool has_counter;
    uint8_t cnt_length_bit;
    uint8_t btn_length_bit;
    const char* display_name;
} FiatVariantParams;

static const FiatVariantParams fiat_variant_params[FIAT_VARIANT_COUNT] = {
    {
        .variant = FiatVariantV3,
        .block_const = &subghz_protocol_fiat_v3_const,
        .min_gap = FIAT_V3_MIN_GAP,
        .frame_bits = FIAT_V3_FRAME_BITS,
        .max_bits = FIAT_V3_MAX_BITS,
        .preamble_bits = FIAT_V3_PREAMBLE_BITS,
        .btn_max = 3,
        .has_counter = false,
        .cnt_length_bit = 0,
        .btn_length_bit = 2,
        .display_name = "Fiat V3",
    },
    {
        .variant = FiatVariantV5,
        .block_const = &subghz_protocol_fiat_v5_const,
        .min_gap = FIAT_V5_MIN_GAP,
        .frame_bits = FIAT_V5_FRAME_BITS,
        .max_bits = FIAT_V5_MAX_BITS,
        .preamble_bits = FIAT_V5_PREAMBLE_BITS,
        .btn_max = 3,
        .has_counter = true,
        .cnt_length_bit = 8,
        .btn_length_bit = 8,
        .display_name = "Fiat V5",
    },
};

static const FiatVariantParams* fiat_get_params(FiatVariant v) {
    return &fiat_variant_params[FIAT_VARIANT_IDX(v)];
}

#define FIAT_V3_BTN_LOCK   0x00
#define FIAT_V3_BTN_UNLOCK 0x01
#define FIAT_V3_BTN_TRUNK  0x02

#define FIAT_V5_BTN_OPEN  0x80
#define FIAT_V5_BTN_TRUNK 0x40
#define FIAT_V5_BTN_CLOSE 0x20

static bool fiat_v5_is_valid_button(uint8_t btn) {
    return (btn == FIAT_V5_BTN_OPEN || btn == FIAT_V5_BTN_TRUNK || btn == FIAT_V5_BTN_CLOSE);
}

typedef enum {
    FiatDecoderStepReset = 0,
    FiatDecoderStepData = 1,
} FiatDecoderStep;

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    FiatVariant variant;
    uint8_t manchester_state;
    uint8_t bits[150];
    uint16_t bit_count;

    union {
        struct {
            uint32_t fix_value;
            uint8_t btn;
            uint8_t status_bits;
            uint64_t hop_value;
            uint8_t hop_low;
            uint16_t timing_count;
            uint32_t preamble_gap_sum;
            uint16_t preamble_gap_count;
            bool preamble_ended;
        } v3;
        struct {
            uint32_t fix_value;
            uint8_t type_byte;
            uint8_t btn;
            uint8_t seed;
            uint8_t cnt;
            uint32_t hop_value;
        } v5;
    } extra;
} SubGhzProtocolDecoderFiat;

typedef struct {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    FiatVariant variant;

    union {
        struct {
            uint32_t fix_value;
            uint8_t btn;
            uint8_t status_bits;
            uint64_t hop_value;
            uint8_t hop_low;
        } v3;
        struct {
            uint32_t fix_value;
            uint8_t type_byte;
            uint8_t btn;
            uint8_t seed;
            uint8_t cnt;
            uint32_t hop_value;
        } v5;
    } extra;
} SubGhzProtocolEncoderFiat;

const SubGhzProtocolDecoder subghz_protocol_fiat_decoder = {
    .alloc = subghz_protocol_decoder_fiat_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_fiat_feed,
    .reset = subghz_protocol_decoder_fiat_reset,
    .get_hash_data = subghz_protocol_decoder_fiat_get_hash_data,
    .serialize = subghz_protocol_decoder_fiat_serialize,
    .deserialize = subghz_protocol_decoder_fiat_deserialize,
    .get_string = subghz_protocol_decoder_fiat_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_fiat_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_fiat_v2_v3_v5");

const SubGhzProtocol subghz_protocol_fiat_v2_v3_v5 = {
    .name = SUBGHZ_PROTOCOL_FIAT_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_fiat_decoder,
    .encoder = &subghz_protocol_fiat_encoder_ext.vtable,
};

static uint8_t fiat_v3_hop_btn_extract(uint64_t hop) {

    const uint64_t mask = (3ULL << 4) | (3ULL << 12) | (3ULL << 20) | (3ULL << 28) | (3ULL << 36);
    uint64_t masked = hop & mask;

    masked ^= masked >> 32;
    masked ^= masked >> 16;
    masked ^= masked >> 8;
    masked ^= masked >> 4;
    masked ^= masked >> 2;
    masked ^= masked >> 1;
    return (uint8_t)(masked & 1);
}

static bool fiat_v3_hop_invariants_check(uint64_t hop) {

    const uint64_t r1 = (3ULL << 5) | (3ULL << 13) | (3ULL << 21) | (3ULL << 29) | (3ULL << 37);

    const uint64_t r2 = (5ULL << 5) | (5ULL << 13) | (5ULL << 21) | (5ULL << 29) | (5ULL << 37);

    const uint64_t r3 = (1ULL << 5) | (1ULL << 8) | (1ULL << 13) | (1ULL << 16) |
                         (1ULL << 21) | (1ULL << 24) | (1ULL << 29) | (1ULL << 32) |
                         (1ULL << 37) | (1ULL << 40);

    const uint64_t r4 = (1ULL << 1) | (1ULL << 9) | (1ULL << 17) | (1ULL << 25) |
                         (1ULL << 33) | (1ULL << 41);

    const uint64_t masks[] = {r1, r2, r3, r4};
    for(int i = 0; i < 4; i++) {
        uint64_t v = hop & masks[i];
        v ^= v >> 32;
        v ^= v >> 16;
        v ^= v >> 8;
        v ^= v >> 4;
        v ^= v >> 2;
        v ^= v >> 1;
        if(v & 1) return false;
    }
    return true;
}

static void fiat_v3_pack_generic(
    SubGhzBlockGeneric* generic,
    uint32_t fix,
    uint8_t btn,
    uint64_t hop,
    uint8_t* hop_low_out) {
    generic->data = ((uint64_t)(fix & 0x3FFFFF) << 42) | ((uint64_t)(btn & 0x3) << 40) |
                    ((hop >> 4) & 0xFFFFFFFFFFULL);
    generic->data_count_bit = FIAT_V3_FRAME_BITS;
    generic->serial = fix;
    generic->btn = btn;
    generic->cnt = 0;
    if(hop_low_out) *hop_low_out = hop & 0xF;
}

static void fiat_v3_unpack_generic(
    const SubGhzBlockGeneric* generic,
    uint8_t hop_low,
    uint32_t* fix_out,
    uint8_t* btn_out,
    uint64_t* hop_out) {
    uint32_t fix = (generic->data >> 42) & 0x3FFFFF;
    uint8_t btn = (generic->data >> 40) & 0x3;
    uint64_t hop = ((generic->data & 0xFFFFFFFFFFULL) << 4) | (hop_low & 0xF);
    if(fix_out) *fix_out = fix;
    if(btn_out) *btn_out = btn;
    if(hop_out) *hop_out = hop;
}

static void fiat_v5_pack_generic(
    SubGhzBlockGeneric* generic,
    uint32_t fix,
    uint8_t btn,
    uint8_t cnt,
    uint32_t hop) {
    generic->data = ((uint64_t)fix << 32) | ((uint64_t)hop & 0xFFFFFFFF);
    generic->data_count_bit = FIAT_V5_FRAME_BITS;
    generic->serial = fix;
    generic->btn = btn;
    generic->cnt = cnt;
}

static void fiat_v5_unpack_generic(
    const SubGhzBlockGeneric* generic,
    uint32_t* fix_out,
    uint32_t* hop_out) {
    if(fix_out) *fix_out = (uint32_t)(generic->data >> 32);
    if(hop_out) *hop_out = (uint32_t)(generic->data & 0xFFFFFFFF);
}

static void fiat_process_frame(SubGhzProtocolDecoderFiat* instance) {
    uint16_t start = 0;
    while(start < instance->bit_count && instance->bits[start] == 1) {
        start++;
    }
    uint16_t preamble_len = start;
    uint16_t data_len = instance->bit_count - start;

    switch(instance->variant) {
    case FiatVariantV3: {
        if(preamble_len < FIAT_V3_PREAMBLE_MIN || data_len < FIAT_V3_MIN_FRAME_BITS) return;
        if(preamble_len > FIAT_V3_PREAMBLE_MAX) return;
        if(instance->bit_count >= FIAT_V3_MAX_BITS) return;
        if(instance->extra.v3.timing_count > FIAT_V3_MAX_TIMINGS) return;

        if(instance->extra.v3.preamble_gap_count > 0) {
            uint32_t avg_gap =
                instance->extra.v3.preamble_gap_sum / instance->extra.v3.preamble_gap_count;
            if(avg_gap < FIAT_V3_MIN_PREAMBLE_GAP) return;
        }

        if(data_len > FIAT_V3_FRAME_BITS) data_len = FIAT_V3_FRAME_BITS;

        uint32_t fix = 0;
        for(uint16_t i = 0; i < 22 && i < data_len; i++) {
            fix = (fix << 1) | instance->bits[start + i];
        }
        uint8_t btn = 0;
        for(uint16_t i = 22; i < 24 && i < data_len; i++) {
            btn = (btn << 1) | instance->bits[start + i];
        }
        uint8_t status = 0;
        for(uint16_t i = 24; i < 26 && i < data_len; i++) {
            status = (status << 1) | instance->bits[start + i];
        }
        uint64_t hop = 0;
        for(uint16_t i = 26; i < 70 && i < data_len; i++) {
            hop = (hop << 1) | instance->bits[start + i];
        }

        if(data_len >= FIAT_V3_FRAME_BITS) {
            if((hop >> 42) != 0) return;
            if((hop & 1) != 1) return;
        }

        instance->extra.v3.fix_value = fix;
        instance->extra.v3.btn = btn;
        instance->extra.v3.status_bits = status;
        instance->extra.v3.hop_value = hop;
        fiat_v3_pack_generic(&instance->generic, fix, btn, hop, &instance->extra.v3.hop_low);

        uint8_t hop_btn = fiat_v3_hop_btn_extract(hop);
        if(hop_btn != btn) return;

        if(!fiat_v3_hop_invariants_check(hop)) return;
        instance->generic.extra_check = true;
        instance->generic.crc_check = true;
    } break;

    case FiatVariantV5: {
        if(preamble_len < FIAT_V5_PREAMBLE_MIN || data_len < FIAT_V5_MIN_FRAME_BITS) return;
        if(data_len > FIAT_V5_FRAME_BITS) data_len = FIAT_V5_FRAME_BITS;

        uint32_t fix = 0;
        for(uint16_t i = 0; i < 32 && i < data_len; i++) {
            fix = (fix << 1) | instance->bits[start + i];
        }
        uint8_t type_byte = 0;
        for(uint16_t i = 32; i < 40 && i < data_len; i++) {
            type_byte = (type_byte << 1) | instance->bits[start + i];
        }
        uint8_t btn = 0;
        for(uint16_t i = 40; i < 48 && i < data_len; i++) {
            btn = (btn << 1) | instance->bits[start + i];
        }
        uint8_t seed = 0;
        for(uint16_t i = 48; i < 56 && i < data_len; i++) {
            seed = (seed << 1) | instance->bits[start + i];
        }
        uint8_t cnt = 0;
        for(uint16_t i = 56; i < 64 && i < data_len; i++) {
            cnt = (cnt << 1) | instance->bits[start + i];
        }
        uint32_t hop = 0;
        for(uint16_t i = 64; i < 96 && i < data_len; i++) {
            hop = (hop << 1) | instance->bits[start + i];
        }

        if(!fiat_v5_is_valid_button(btn)) return;

        if(data_len >= FIAT_V5_FRAME_BITS) {
            if((hop & 1) != 1) return;
        }

        instance->extra.v5.fix_value = fix;
        instance->extra.v5.type_byte = type_byte;
        instance->extra.v5.btn = btn;
        instance->extra.v5.seed = seed;
        instance->extra.v5.cnt = cnt;
        instance->extra.v5.hop_value = hop;
        fiat_v5_pack_generic(&instance->generic, fix, btn, cnt, hop);
    } break;

    default:
        return;
    }

    instance->decoder.decode_data = instance->generic.data;

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

static void
    fiat_decode_timing(SubGhzProtocolDecoderFiat* instance, bool level, uint32_t duration) {
    const FiatVariantParams* params = fiat_get_params(instance->variant);
    const SubGhzBlockConst* bc = params->block_const;

    uint8_t timing_type;
    bool valid = false;

    if(DURATION_DIFF(duration, bc->te_short) <= bc->te_delta) {
        timing_type = level ? 0 : 2;
        valid = true;
    } else if(DURATION_DIFF(duration, bc->te_long) <= bc->te_delta) {
        timing_type = level ? 4 : 6;
        valid = true;
    }

    if(valid) {

        if(instance->variant == FiatVariantV3) {
            instance->extra.v3.timing_count++;
            if(!level && !instance->extra.v3.preamble_ended) {
                instance->extra.v3.preamble_gap_sum += duration;
                instance->extra.v3.preamble_gap_count++;
            }
            if(instance->extra.v3.timing_count > FIAT_V3_MAX_TIMINGS) {
                instance->decoder.parser_step = FiatDecoderStepReset;
                instance->bit_count = 0;
                instance->manchester_state = 1;
                instance->extra.v3.timing_count = 0;
                instance->extra.v3.preamble_gap_sum = 0;
                instance->extra.v3.preamble_gap_count = 0;
                instance->extra.v3.preamble_ended = false;
                return;
            }
        }

        uint8_t bit_value = 0;
        if(fiat_manchester_decode(&instance->manchester_state, timing_type, &bit_value)) {
            if(instance->bit_count < params->max_bits) {
                instance->bits[instance->bit_count++] = bit_value;
            }

            if(instance->variant == FiatVariantV3 && bit_value == 0 &&
               !instance->extra.v3.preamble_ended) {
                instance->extra.v3.preamble_ended = true;
            }

            if(instance->bit_count >= params->max_bits) {
                fiat_process_frame(instance);
                instance->decoder.parser_step = FiatDecoderStepReset;
                instance->bit_count = 0;
                instance->manchester_state = 1;
                if(instance->variant == FiatVariantV3) {
                    instance->extra.v3.timing_count = 0;
                    instance->extra.v3.preamble_gap_sum = 0;
                    instance->extra.v3.preamble_gap_count = 0;
                    instance->extra.v3.preamble_ended = false;
                }
            }
        }
    } else {
        if(instance->bit_count >= bc->min_count_bit_for_found) {
            fiat_process_frame(instance);
        }
        instance->decoder.parser_step = FiatDecoderStepReset;
        instance->bit_count = 0;
        instance->manchester_state = 1;
        if(instance->variant == FiatVariantV3) {
            instance->extra.v3.timing_count = 0;
            instance->extra.v3.preamble_gap_sum = 0;
            instance->extra.v3.preamble_gap_count = 0;
            instance->extra.v3.preamble_ended = false;
        }
    }
}

void* subghz_protocol_decoder_fiat_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);

    SubGhzProtocolDecoderFiat* instance = malloc(sizeof(SubGhzProtocolDecoderFiat));
    instance->base.protocol = &subghz_protocol_fiat_v2_v3_v5;
    instance->generic.protocol_name = subghz_protocol_fiat_v2_v3_v5.name;
    instance->variant = FiatVariantV3;
    instance->manchester_state = 1;
    instance->bit_count = 0;
    memset(instance->bits, 0, sizeof(instance->bits));
    memset(&instance->extra, 0, sizeof(instance->extra));

    return instance;
}

void subghz_protocol_decoder_fiat_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderFiat* instance = context;

    instance->decoder.parser_step = FiatDecoderStepReset;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->manchester_state = 1;
    instance->bit_count = 0;
    memset(&instance->extra, 0, sizeof(instance->extra));
}

void subghz_protocol_decoder_fiat_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderFiat* instance = context;

    switch(instance->decoder.parser_step) {
    case FiatDecoderStepReset: {

        int best_idx = -1;
        uint32_t best_diff = UINT32_MAX;

        for(int i = 0; i < FIAT_VARIANT_COUNT; i++) {
            const SubGhzBlockConst* bc = fiat_variant_params[i].block_const;
            uint32_t diff = DURATION_DIFF(duration, bc->te_short);
            if(diff <= bc->te_delta && diff < best_diff) {
                best_diff = diff;
                best_idx = i;
            }
        }

        if(best_idx >= 0) {
            instance->variant = fiat_variant_params[best_idx].variant;
            instance->decoder.parser_step = FiatDecoderStepData;
            instance->manchester_state = 1;
            instance->bit_count = 0;
            memset(&instance->extra, 0, sizeof(instance->extra));
            fiat_decode_timing(instance, level, duration);
        }
    } break;

    case FiatDecoderStepData: {
        const FiatVariantParams* params = fiat_get_params(instance->variant);

        if(duration > params->min_gap) {
            if(instance->bit_count >= params->block_const->min_count_bit_for_found) {
                fiat_process_frame(instance);
            }
            instance->decoder.parser_step = FiatDecoderStepReset;
            instance->bit_count = 0;
            instance->manchester_state = 1;
            memset(&instance->extra, 0, sizeof(instance->extra));
            return;
        }

        fiat_decode_timing(instance, level, duration);
    } break;

    default:
        instance->decoder.parser_step = FiatDecoderStepReset;
        break;
    }
}

uint8_t subghz_protocol_decoder_fiat_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderFiat* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, sizeof(uint64_t));
}

SubGhzProtocolStatus subghz_protocol_decoder_fiat_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderFiat* dec = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&dec->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t variant_temp = (uint32_t)dec->variant;
    if(!flipper_format_insert_or_update_uint32(flipper_format, "Variant", &variant_temp, 1)) {
        return SubGhzProtocolStatusError;
    }

    switch(dec->variant) {
    case FiatVariantV3: {
        uint32_t hop_low_temp = dec->extra.v3.hop_low;
        if(!flipper_format_insert_or_update_uint32(flipper_format, "HopLow", &hop_low_temp, 1)) {
            return SubGhzProtocolStatusError;
        }
        uint32_t status_temp = dec->extra.v3.status_bits;
        if(!flipper_format_insert_or_update_uint32(flipper_format, "Status", &status_temp, 1)) {
            return SubGhzProtocolStatusError;
        }
    } break;

    case FiatVariantV5: {
        uint32_t type_temp = dec->extra.v5.type_byte;
        if(!flipper_format_insert_or_update_uint32(flipper_format, "Type", &type_temp, 1)) {
            return SubGhzProtocolStatusError;
        }
        uint32_t seed_temp = dec->extra.v5.seed;
        if(!flipper_format_insert_or_update_uint32(flipper_format, "Seed", &seed_temp, 1)) {
            return SubGhzProtocolStatusError;
        }
        uint32_t btn_temp = dec->extra.v5.btn;
        if(!flipper_format_insert_or_update_uint32(flipper_format, "Btn", &btn_temp, 1)) {
            return SubGhzProtocolStatusError;
        }
        uint32_t cnt_temp = dec->extra.v5.cnt;
        if(!flipper_format_insert_or_update_uint32(flipper_format, "Cnt", &cnt_temp, 1)) {
            return SubGhzProtocolStatusError;
        }
    } break;
    }

    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_fiat_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderFiat* dec = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize(&dec->generic, flipper_format);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t variant_temp = 0;
    if(flipper_format_read_uint32(flipper_format, "Variant", &variant_temp, 1)) {
        dec->variant = (FiatVariant)variant_temp;
    } else {
        switch(dec->generic.data_count_bit) {
        case FIAT_V3_FRAME_BITS:
            dec->variant = FiatVariantV3;
            break;
        case FIAT_V5_FRAME_BITS:
            dec->variant = FiatVariantV5;
            break;
        default:
            return SubGhzProtocolStatusErrorValueBitCount;
        }
    }

    const FiatVariantParams* params = fiat_get_params(dec->variant);

    if(dec->generic.data_count_bit != params->frame_bits) {
        return SubGhzProtocolStatusErrorValueBitCount;
    }

    switch(dec->variant) {
    case FiatVariantV3: {
        uint32_t hop_low_temp = 0;
        flipper_format_read_uint32(flipper_format, "HopLow", &hop_low_temp, 1);
        dec->extra.v3.hop_low = hop_low_temp & 0xF;

        uint32_t status_temp = 0;
        flipper_format_read_uint32(flipper_format, "Status", &status_temp, 1);
        dec->extra.v3.status_bits = status_temp & 0x3;

        uint32_t fix;
        uint8_t btn;
        uint64_t hop;
        fiat_v3_unpack_generic(&dec->generic, dec->extra.v3.hop_low, &fix, &btn, &hop);
        dec->extra.v3.fix_value = fix;
        dec->extra.v3.btn = btn;
        dec->extra.v3.hop_value = hop;

        dec->generic.serial = fix;
        dec->generic.btn = btn;
    } break;

    case FiatVariantV5: {
        uint32_t type_temp = 0;
        flipper_format_read_uint32(flipper_format, "Type", &type_temp, 1);
        dec->extra.v5.type_byte = type_temp & 0xFF;

        uint32_t seed_temp = 0;
        flipper_format_read_uint32(flipper_format, "Seed", &seed_temp, 1);
        dec->extra.v5.seed = seed_temp & 0xFF;

        uint32_t btn_temp = 0;
        if(flipper_format_read_uint32(flipper_format, "Btn", &btn_temp, 1)) {
            dec->extra.v5.btn = btn_temp & 0xFF;
            dec->generic.btn = dec->extra.v5.btn;
        }

        uint32_t cnt_temp = 0;
        if(flipper_format_read_uint32(flipper_format, "Cnt", &cnt_temp, 1)) {
            dec->extra.v5.cnt = cnt_temp & 0xFF;
            dec->generic.cnt = dec->extra.v5.cnt;
        }

        fiat_v5_unpack_generic(&dec->generic, &dec->extra.v5.fix_value, &dec->extra.v5.hop_value);
        dec->generic.serial = dec->extra.v5.fix_value;
    } break;
    }

    subghz_custom_btn_set_original_if_unset(dec->generic.btn);
    subghz_custom_btn_set_max(params->btn_max);

    return SubGhzProtocolStatusOk;
}

void subghz_protocol_decoder_fiat_get_string(void* context, FuriString* output) {
    SubGhzProtocolDecoderFiat* dec = context;
    const SubGhzDecodeInput input = {
        .decoder = context,
        .generic = &dec->generic,
        .extra = NULL,
        .extra_count = 0,
        .aux = NULL,
    };
    subghz_decode_plugin_get_string(dec->generic.protocol_name, &input, output);
}

