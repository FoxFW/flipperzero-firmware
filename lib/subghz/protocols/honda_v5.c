#include "honda_v5.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <string.h>
#include "common.h"
#include <lib/subghz/blocks/bitbuf.h>

#define TAG "HondaV5"

// Honda V5 -- ported from firmware honda_v5.c via KeeLoq-RX arke_honda.py
// (class HondaV5). Manchester, te 63/126 us, custom Honda LUT {0x01,0x91,0x9B,0xFB}.
// Two frame variants auto-detected from the collected bit stream:
//   * Standard 135-bit: header 0x6C0F62, CRC-8 (MSB poly 0x2F, init 0x03) over
//     bytes 0..15 compared to byte 16. serial(32) | button | counter(16) |
//     rolling(32). Leading preamble 1-bits are skipped and up to two absorbed
//     leading data 1-bits are restored.
//   * Alternate 168-bit: header 0x6428, CRC-8 reflected (poly 0x4D, init 0) over
//     bytes 0..19 compared to byte 20. serial(32 @ bytes13..16) | button(byte17).
// Decode-only; .encoder = NULL.

#define HONDA_V5_TE_SHORT 63
#define HONDA_V5_TE_LONG  126
#define HONDA_V5_TE_DELTA 20
#define HONDA_V5_MIN_BITS 130
#define HONDA_V5_MAX_BITS 400
#define HONDA_V5_STD_BITS 135
#define HONDA_V5_ALT_BITS 168

static const SubGhzBlockConst subghz_protocol_honda_v5_const = {
    .te_short = HONDA_V5_TE_SHORT,
    .te_long = HONDA_V5_TE_LONG,
    .te_delta = HONDA_V5_TE_DELTA,
    .min_count_bit_for_found = HONDA_V5_STD_BITS,
};

static const uint8_t honda_man_lut[4] = {0x01, 0x91, 0x9B, 0xFB};

struct SubGhzProtocolDecoderHondaV5 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t parser_step;
    uint8_t man_state;
    uint8_t bits[SUBGHZ_BITBUF_BYTES(HONDA_V5_MAX_BITS)];
    uint16_t bit_count;

    uint16_t variant; // 135 or 168
    uint64_t data_mid; // frame bytes 8..15
    uint32_t data_ext; // frame bytes 16..19 (alt only)
    uint32_t serial;
    uint8_t btn;
    uint16_t cnt;
    uint32_t rolling;
    uint8_t crc;
    bool crc_ok;
    bool rtx;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderHondaV5);


typedef enum {
    HondaV5StepReset = 0,
    HondaV5StepCollect = 1,
} HondaV5Step;

static bool honda_v5_man_step(SubGhzProtocolDecoderHondaV5* inst, uint8_t tt, uint8_t* bit) {
    if(tt == 8) {
        inst->man_state = 1;
        return false;
    }
    if(inst->man_state >= 4) {
        inst->man_state = 1;
        return false;
    }
    uint8_t ns = (honda_man_lut[inst->man_state] >> tt) & 0x3;
    if(inst->man_state == ns) return false;
    inst->man_state = ns;
    if(ns == 2) {
        *bit = 0;
        return true;
    }
    if(ns == 1) {
        *bit = 1;
        return true;
    }
    return false;
}

static uint8_t honda_v5_timing_type(uint32_t duration, bool level) {
    if(DURATION_DIFF(duration, HONDA_V5_TE_SHORT) <= HONDA_V5_TE_DELTA) return level ? 0 : 2;
    if(DURATION_DIFF(duration, HONDA_V5_TE_LONG) <= HONDA_V5_TE_DELTA) return level ? 4 : 6;
    return 8;
}

// CRC-8 MSB-first (mirror arke_core.crc8).
static uint8_t honda_v5_crc8(const uint8_t* m, size_t n, uint8_t poly, uint8_t init) {
    uint8_t r = init;
    for(size_t i = 0; i < n; i++) {
        r ^= m[i];
        for(uint8_t k = 0; k < 8; k++) r = (r & 0x80) ? (uint8_t)((r << 1) ^ poly) : (uint8_t)(r << 1);
    }
    return r;
}

// CRC-8 reflected / LSB-first (mirror arke_honda._crc8_reflected).
static uint8_t honda_v5_crc8_reflected(const uint8_t* m, size_t n, uint8_t poly, uint8_t init) {
    uint8_t crc = init;
    for(size_t i = 0; i < n; i++) {
        crc ^= m[i];
        for(uint8_t k = 0; k < 8; k++) crc = (crc & 1) ? (uint8_t)((crc >> 1) ^ poly) : (uint8_t)(crc >> 1);
    }
    return crc;
}

static void honda_v5_reset_collect(SubGhzProtocolDecoderHondaV5* inst) {
    inst->parser_step = HondaV5StepReset;
    inst->man_state = 1;
    inst->bit_count = 0;
}

static void honda_v5_emit(SubGhzProtocolDecoderHondaV5* inst, const uint8_t* raw, int nbytes) {
    uint64_t top = 0;
    for(int i = 0; i < 8; i++) top = (top << 8) | raw[i];
    uint64_t mid = 0;
    for(int i = 8; i < 16; i++) mid = (mid << 8) | raw[i];
    uint32_t ext = 0;
    for(int i = 16; i < 20 && i < nbytes; i++) ext = (ext << 8) | raw[i];

    inst->data_mid = mid;
    inst->data_ext = ext;

    inst->generic.data = top;
    inst->generic.data_count_bit = inst->variant;
    inst->generic.serial = inst->serial;
    inst->generic.btn = inst->btn;
    inst->generic.cnt = inst->cnt;
    inst->decoder.decode_data = top;
    inst->decoder.decode_count_bit = inst->variant;

    if(inst->base.callback) inst->base.callback(&inst->base, inst->base.context);
}

// Mirror HondaV5._process().
static void honda_v5_process(SubGhzProtocolDecoderHondaV5* inst) {
    int n = inst->bit_count;
    if(n > HONDA_V5_MAX_BITS) n = HONDA_V5_MAX_BITS;
    uint8_t* bits = inst->bits;

    int start = 0;
    while(start < n && subghz_bitbuf_get(bits, start) == 1) start++;
    int data_len = n - start;

    // ---- alternate 168-bit variant ----
    if(data_len >= HONDA_V5_ALT_BITS) {
        uint8_t raw[22];
        memset(raw, 0, sizeof(raw));
        for(int i = 0; i < HONDA_V5_ALT_BITS; i++) {
            if(start + i >= n) break;
            int bi = i / 8;
            if(bi < 21 && subghz_bitbuf_get(bits, start + i)) raw[bi] |= (uint8_t)(1 << (7 - (i % 8)));
        }
        uint16_t alt_header = (uint16_t)((raw[0] << 8) | raw[1]);
        if(alt_header == 0x6428) {
            uint8_t computed = honda_v5_crc8_reflected(raw, 20, 0x4D, 0);
            uint8_t frame_crc = raw[20];
            uint32_t serial =
                ((uint32_t)raw[13] << 24) | ((uint32_t)raw[14] << 16) | ((uint32_t)raw[15] << 8) | raw[16];
            uint16_t counter = raw[14];
            uint8_t button = raw[17];
            if(computed == frame_crc) {
                inst->variant = HONDA_V5_ALT_BITS;
                inst->serial = serial;
                inst->btn = button;
                inst->cnt = counter;
                inst->rolling = 0;
                inst->crc = computed;
                inst->crc_ok = true;
                inst->rtx = false;
                honda_v5_emit(inst, raw, 20);
            }
            return;
        }
    }

    // ---- standard 135-bit ----
    if(data_len < HONDA_V5_STD_BITS) {
        int missing = HONDA_V5_STD_BITS - data_len;
        if(missing > 0 && missing <= 2 && start >= missing) {
            for(int k = 0; k < missing; k++) {
                start--;
                subghz_bitbuf_set(bits, start, 1);
            }
            data_len = HONDA_V5_STD_BITS;
        }
    }
    if(data_len < HONDA_V5_STD_BITS) return;

    uint8_t raw[17];
    memset(raw, 0, sizeof(raw));
    for(int i = 0; i < HONDA_V5_STD_BITS; i++) {
        if(start + i >= n) break;
        int pos = i + 1;
        int bi = pos / 8;
        if(bi < 17) raw[bi] |= (uint8_t)(subghz_bitbuf_get(bits, start + i) << (7 - (pos % 8)));
    }
    uint32_t header = ((uint32_t)raw[0] << 16) | ((uint32_t)raw[1] << 8) | raw[2];
    if(header != 0x6C0F62) return;
    uint8_t retransmit = raw[3];
    uint32_t serial =
        ((uint32_t)raw[4] << 24) | ((uint32_t)raw[5] << 16) | ((uint32_t)raw[6] << 8) | raw[7];
    uint8_t button = raw[8];
    uint16_t counter = (uint16_t)(((uint16_t)raw[10] << 8) | raw[11]);
    uint32_t rolling =
        ((uint32_t)raw[12] << 24) | ((uint32_t)raw[13] << 16) | ((uint32_t)raw[14] << 8) | raw[15];
    uint8_t tail = raw[16];
    uint8_t computed = honda_v5_crc8(raw, 16, 0x2F, 0x03);
    if(computed != tail) return;

    inst->variant = HONDA_V5_STD_BITS;
    inst->serial = serial;
    inst->btn = button;
    inst->cnt = counter;
    inst->rolling = rolling;
    inst->crc = computed;
    inst->crc_ok = true;
    inst->rtx = (retransmit == 0x0A);
    honda_v5_emit(inst, raw, 17);
}

static void honda_v5_decode_timing(SubGhzProtocolDecoderHondaV5* inst, bool level, uint32_t duration) {
    uint8_t tt = honda_v5_timing_type(duration, level);
    if(tt != 8) {
        uint8_t bit = 0;
        if(honda_v5_man_step(inst, tt, &bit)) {
            if(inst->bit_count < HONDA_V5_MAX_BITS) subghz_bitbuf_set(inst->bits, inst->bit_count++, bit);
            if(inst->bit_count >= HONDA_V5_MAX_BITS) {
                honda_v5_process(inst);
                honda_v5_reset_collect(inst);
            }
        }
    } else {
        if(inst->bit_count >= HONDA_V5_MIN_BITS) honda_v5_process(inst);
        honda_v5_reset_collect(inst);
    }
}

void* subghz_protocol_decoder_honda_v5_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderHondaV5* instance = calloc(1, sizeof(SubGhzProtocolDecoderHondaV5));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_honda_v5;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->man_state = 1;
    return instance;
}



void subghz_protocol_decoder_honda_v5_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV5* instance = context;
    instance->parser_step = HondaV5StepReset;
    instance->man_state = 1;
    instance->bit_count = 0;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_honda_v5_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV5* instance = context;
    uint32_t gap = subghz_protocol_honda_v5_const.te_long +
                   subghz_protocol_honda_v5_const.te_delta * 3;

    switch(instance->parser_step) {
    case HondaV5StepReset:
        if(DURATION_DIFF(duration, HONDA_V5_TE_SHORT) <= HONDA_V5_TE_DELTA) {
            instance->parser_step = HondaV5StepCollect;
            instance->man_state = 1;
            instance->bit_count = 0;
            honda_v5_decode_timing(instance, level, duration);
        }
        break;
    case HondaV5StepCollect:
        if(duration > gap) {
            if(instance->bit_count >= HONDA_V5_MIN_BITS) honda_v5_process(instance);
            honda_v5_reset_collect(instance);
            return;
        }
        honda_v5_decode_timing(instance, level, duration);
        break;
    default:
        break;
    }
}



SubGhzProtocolStatus subghz_protocol_decoder_honda_v5_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV5* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t serial = instance->serial;
    uint32_t btn = instance->btn;
    uint32_t cnt = instance->cnt;
    uint32_t crc = instance->crc;
    uint32_t rolling = instance->rolling;
    uint32_t variant = instance->variant;
    uint32_t mid_hi = (uint32_t)(instance->data_mid >> 32);
    uint32_t mid_lo = (uint32_t)(instance->data_mid & 0xFFFFFFFFU);
    uint32_t ext = instance->data_ext;
    if(!flipper_format_write_uint32(flipper_format, "Serial", &serial, 1) ||
       !flipper_format_write_uint32(flipper_format, "Btn", &btn, 1) ||
       !flipper_format_write_uint32(flipper_format, "Cnt", &cnt, 1) ||
       !flipper_format_write_uint32(flipper_format, "CRC", &crc, 1) ||
       !flipper_format_write_uint32(flipper_format, "Rolling", &rolling, 1) ||
       !flipper_format_write_uint32(flipper_format, "Variant", &variant, 1) ||
       !flipper_format_write_uint32(flipper_format, "MidHi", &mid_hi, 1) ||
       !flipper_format_write_uint32(flipper_format, "MidLo", &mid_lo, 1) ||
       !flipper_format_write_uint32(flipper_format, "Ext", &ext, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_honda_v5_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV5* instance = context;
    // Two valid lengths (135 / 168), so accept either rather than a single check.
    SubGhzProtocolStatus status =
        subghz_block_generic_deserialize(&instance->generic, flipper_format);
    if(status != SubGhzProtocolStatusOk) return status;
    if(instance->generic.data_count_bit != HONDA_V5_STD_BITS &&
       instance->generic.data_count_bit != HONDA_V5_ALT_BITS) {
        return SubGhzProtocolStatusErrorValueBitCount;
    }

    uint32_t v = 0;
    flipper_format_rewind(flipper_format);
    instance->serial = flipper_format_read_uint32(flipper_format, "Serial", &v, 1) ? v : 0;
    flipper_format_rewind(flipper_format);
    instance->btn = flipper_format_read_uint32(flipper_format, "Btn", &v, 1) ? (uint8_t)v : 0;
    flipper_format_rewind(flipper_format);
    instance->cnt = flipper_format_read_uint32(flipper_format, "Cnt", &v, 1) ? (uint16_t)v : 0;
    flipper_format_rewind(flipper_format);
    instance->crc = flipper_format_read_uint32(flipper_format, "CRC", &v, 1) ? (uint8_t)v : 0;
    flipper_format_rewind(flipper_format);
    instance->rolling = flipper_format_read_uint32(flipper_format, "Rolling", &v, 1) ? v : 0;
    flipper_format_rewind(flipper_format);
    instance->variant = flipper_format_read_uint32(flipper_format, "Variant", &v, 1) ?
                            (uint16_t)v :
                            (uint16_t)instance->generic.data_count_bit;
    uint32_t hi = 0, lo = 0, ext = 0;
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "MidHi", &hi, 1);
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "MidLo", &lo, 1);
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Ext", &ext, 1);
    instance->data_mid = ((uint64_t)hi << 32) | lo;
    instance->data_ext = ext;
    instance->crc_ok = true;
    instance->rtx = false;

    instance->generic.serial = instance->serial;
    instance->generic.btn = instance->btn;
    instance->generic.cnt = instance->cnt;
    instance->decoder.decode_data = instance->generic.data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

const SubGhzProtocolDecoder subghz_protocol_honda_v5_decoder = {
    .alloc = subghz_protocol_decoder_honda_v5_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_honda_v5_feed,
    .reset = subghz_protocol_decoder_honda_v5_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_honda_v5_serialize,
    .deserialize = subghz_protocol_decoder_honda_v5_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

const SubGhzProtocol subghz_protocol_honda_v5 = {
    .name = HONDA_V5_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_honda_v5_decoder,
    .encoder = NULL,
};
