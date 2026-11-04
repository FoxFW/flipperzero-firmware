#include "porsche_touareg.h"

/* ==== Self-contained sub-GHz helpers (de-refactored from the ProtoPirate/applab
 * "protocols_common" pp-framework) so this decoder depends only on the core
 * sub-GHz lib and carries no shared helper unit into lib/. All helpers are
 * static inline: unused ones are dropped with no -Wunused-function warning, and
 * the TX scratch buffer is a function-local static (no file-scope unused-var).
 * FoxFW2.0 2026-10-18 internal batch 2. ==== */
#include <furi.h>
#include <string.h>
#include <lib/subghz/types.h>
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>
#include <flipper_format/flipper_format.h>


#ifndef PP_COMPAT_SHIM_DEFINED
#define PP_COMPAT_SHIM_DEFINED
#define FF_BIT                    "Bit"
#define FF_KEY                    "Key"
#define FF_SERIAL                 "Serial"
#define FF_BTN                    "Btn"
#define FF_CNT                    "Cnt"
#define FF_REPEAT                 "Repeat"
#define FF_PROTOCOL               "Protocol"
#define FF_TYPE                   "Type"
#define PP_FIELD_SERIAL           0x01U
#define PP_FIELD_BTN              0x02U
#define PP_FIELD_CNT              0x04U
#define PP_FIELD_TYPE             0x08U
#define PP_ENCODER_REPEAT_MAX     50U
#define PP_SHARED_UPLOAD_CAPACITY 2048U

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
} ProtoPirateDecoderHeader;

typedef struct {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
} ProtoPirateEncoderHeader;

#endif /* PP_COMPAT_SHIM_DEFINED */

#include <string.h>
#include "common.h"
#include "../encode_plugin.h"
#include "pp_auto_common.h"

// Original implementation by @lupettohf

#define PORSCHE_TOUAREG_BIT_COUNT 64
#define PC_TE_SYNC                3370U
#define PC_TE_GAP                 5930U
#define PC_SYNC_MIN               15

static const SubGhzBlockConst subghz_protocol_porsche_touareg_const = {
    .te_short = 1680,
    .te_long = 3370,
    .te_delta = 500,
    .min_count_bit_for_found = PORSCHE_TOUAREG_BIT_COUNT,
};

#define PORSCHE_TOUAREG_SYNC_TX 20U
#define PORSCHE_TOUAREG_UPLOAD_CAPACITY \
    ((PORSCHE_TOUAREG_SYNC_TX * 2U) + 2U + (PORSCHE_TOUAREG_BIT_COUNT * 2U))
_Static_assert(
    PORSCHE_TOUAREG_UPLOAD_CAPACITY <= PP_SHARED_UPLOAD_CAPACITY,
    "PORSCHE_TOUAREG_UPLOAD_CAPACITY exceeds shared upload slab");

typedef enum {
    PorscheTouaregDecoderStepReset = 0,
    PorscheTouaregDecoderStepSync,
    PorscheTouaregDecoderStepGapHigh,
    PorscheTouaregDecoderStepGapLow,
    PorscheTouaregDecoderStepData,
} PorscheTouaregDecoderStep;

struct SubGhzProtocolDecoderPorscheTouareg {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t sync_count;
    uint64_t raw_data;
    uint8_t bit_count;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderPorscheTouareg);

static void porsche_touareg_compute_frame(
    uint32_t serial24,
    uint8_t btn,
    uint16_t counter,
    uint8_t frame_type,
    uint8_t* pkt) {
    uint8_t b0 = (uint8_t)((btn << 4) | (frame_type & 0x07));
    uint8_t b1 = (serial24 >> 16) & 0xFF;
    uint8_t b2 = (serial24 >> 8) & 0xFF;
    uint8_t b3 = serial24 & 0xFF;

    uint16_t cnt = counter + 1;
    uint8_t cnt_lo = cnt & 0xFF;
    uint8_t cnt_hi = (cnt >> 8) & 0xFF;

    uint8_t r_h = b3;
    uint8_t r_m = b1;
    uint8_t r_l = b2;

#define ROTATE24(rh, rm, rl)                 \
    do {                                     \
        uint8_t _ch = ((rh) >> 7) & 1U;      \
        uint8_t _cm = ((rm) >> 7) & 1U;      \
        uint8_t _cl = ((rl) >> 7) & 1U;      \
        (rh) = (uint8_t)(((rh) << 1) | _cm); \
        (rm) = (uint8_t)(((rm) << 1) | _cl); \
        (rl) = (uint8_t)(((rl) << 1) | _ch); \
    } while(0)

    for(uint8_t i = 0; i < 4; i++) {
        ROTATE24(r_h, r_m, r_l);
    }
    for(uint16_t i = 0; i < cnt_lo; i++) {
        ROTATE24(r_h, r_m, r_l);
    }

#undef ROTATE24

    uint8_t a9a = r_h ^ b0;

    uint8_t nb9b_p1 = (uint8_t)((~cnt_lo << 2) & 0xFC) ^ r_m;
    uint8_t nb9b_p2 = (uint8_t)((~cnt_hi << 2) & 0xFC) ^ r_m;
    uint8_t nb9b_p3 = (uint8_t)((~cnt_hi >> 6) & 0x03) ^ r_m;
    uint8_t a9b = (nb9b_p1 & 0xCC) | (nb9b_p2 & 0x30) | (nb9b_p3 & 0x03);

    uint8_t nb9c_p1 = (uint8_t)((~cnt_lo >> 2) & 0x3F) ^ r_l;
    uint8_t nb9c_p2 = (uint8_t)((~cnt_hi & 0x03) << 6) ^ r_l;
    uint8_t nb9c_p3 = (uint8_t)((~cnt_hi >> 2) & 0x3F) ^ r_l;
    uint8_t a9c = (nb9c_p1 & 0x33) | (nb9c_p2 & 0xC0) | (nb9c_p3 & 0x0C);

    pkt[0] = b0;
    pkt[1] = b1;
    pkt[2] = b2;
    pkt[3] = b3;
    pkt[4] = (uint8_t)(((a9a >> 2) & 0x3F) | ((~cnt_lo & 0x03U) << 6));
    pkt[5] = (uint8_t)((~cnt_lo & 0xC0U) | ((a9a & 0x03U) << 4) | (a9b & 0x0CU) |
                       ((~cnt_lo >> 2) & 0x03U));
    pkt[6] = (uint8_t)(((a9b & 0x03U) << 6) | ((a9c >> 2) & 0x3CU) | ((~cnt_lo >> 4) & 0x03U));
    pkt[7] = (uint8_t)(((a9b >> 4) & 0x0FU) | ((a9c & 0x0FU) << 4));
}

static void porsche_touareg_parse_data(SubGhzProtocolDecoderPorscheTouareg* instance) {
    uint8_t pkt[8];
    uint64_t raw = instance->generic.data;

    for(int8_t i = 7; i >= 0; i--) {
        pkt[i] = (uint8_t)(raw & 0xFF);
        raw >>= 8;
    }

    instance->generic.serial = ((uint32_t)pkt[1] << 16) | ((uint32_t)pkt[2] << 8) | pkt[3];
    instance->generic.btn = (uint8_t)(pkt[0] >> 4);
    instance->generic.cnt = 0;

    uint8_t frame_type = pkt[0] & 0x07;
    uint8_t try_pkt[8];
    for(uint16_t try_cnt = 1; try_cnt <= 256; try_cnt++) {
        porsche_touareg_compute_frame(
            instance->generic.serial,
            instance->generic.btn,
            (uint16_t)(try_cnt - 1),
            frame_type,
            try_pkt);
        if(try_pkt[4] == pkt[4] && try_pkt[5] == pkt[5] && try_pkt[6] == pkt[6] &&
           try_pkt[7] == pkt[7]) {
            instance->generic.cnt = try_cnt;
            break;
        }
    }
}

static void porsche_touareg_publish_frame(SubGhzProtocolDecoderPorscheTouareg* instance) {
    instance->generic.data = instance->raw_data;
    instance->generic.data_count_bit = PORSCHE_TOUAREG_BIT_COUNT;
    porsche_touareg_parse_data(instance);

    // parse_data leaves cnt==0 when the counter brute-force failed to
    // reproduce pkt[4..7]; only a real match sets cnt in 1..256. Suppress
    // the callback on no-match so foreign 64-bit frames are not emitted.
    if(instance->generic.cnt != 0 && instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

typedef struct SubGhzProtocolEncoderPorscheTouareg {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint32_t serial;
    uint8_t btn;
    uint16_t cnt;
    uint8_t frame_type;
} SubGhzProtocolEncoderPorscheTouareg;

const SubGhzProtocolDecoder subghz_protocol_porsche_touareg_decoder = {
    .alloc = subghz_protocol_decoder_porsche_touareg_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_porsche_touareg_feed,
    .reset = subghz_protocol_decoder_porsche_touareg_reset,
    .get_hash_data = subghz_protocol_decoder_porsche_touareg_get_hash_data,
    .serialize = subghz_protocol_decoder_porsche_touareg_serialize,
    .deserialize = subghz_protocol_decoder_porsche_touareg_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

/* host bridge: firmware functions/state the SD encoder uses (handed over at load) */
typedef struct {
    __typeof__(&ppa_bytes_to_u64_be) ppa_bytes_to_u64_be;
    __typeof__(&ppa_emit) ppa_emit;
    __typeof__(&ppa_encoder_buffer_ensure) ppa_encoder_buffer_ensure;
    __typeof__(&ppa_encoder_free) ppa_encoder_free;
    __typeof__(&ppa_encoder_read_fields) ppa_encoder_read_fields;
    __typeof__(&ppa_encoder_read_repeat) ppa_encoder_read_repeat;
    __typeof__(&ppa_encoder_stop) ppa_encoder_stop;
    __typeof__(&ppa_encoder_yield) ppa_encoder_yield;
    __typeof__(&ppa_u64_to_bytes_be) ppa_u64_to_bytes_be;
    __typeof__(&ppa_verify_protocol_name) ppa_verify_protocol_name;
} enc_porsche_touareg_host_t;
static const enc_porsche_touareg_host_t enc_porsche_touareg_host = {
    .ppa_bytes_to_u64_be = ppa_bytes_to_u64_be,
    .ppa_emit = ppa_emit,
    .ppa_encoder_buffer_ensure = ppa_encoder_buffer_ensure,
    .ppa_encoder_free = ppa_encoder_free,
    .ppa_encoder_read_fields = ppa_encoder_read_fields,
    .ppa_encoder_read_repeat = ppa_encoder_read_repeat,
    .ppa_encoder_stop = ppa_encoder_stop,
    .ppa_encoder_yield = ppa_encoder_yield,
    .ppa_u64_to_bytes_be = ppa_u64_to_bytes_be,
    .ppa_verify_protocol_name = ppa_verify_protocol_name,
};
static const SubGhzEncoderExternal subghz_protocol_porsche_touareg_encoder_ext = SUBGHZ_ENCODER_EXTERNAL_HOST("enc_porsche_touareg", &enc_porsche_touareg_host);

const SubGhzProtocol porsche_touareg_protocol = {
    .name = PORSCHE_TOUAREG_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_porsche_touareg_decoder,
    .encoder = &subghz_protocol_porsche_touareg_encoder_ext.vtable,
};

void* subghz_protocol_decoder_porsche_touareg_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderPorscheTouareg* instance =
        calloc(1, sizeof(SubGhzProtocolDecoderPorscheTouareg));
    furi_check(instance);
    instance->base.protocol = &porsche_touareg_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_porsche_touareg_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderPorscheTouareg* instance = context;
    instance->decoder.parser_step = PorscheTouaregDecoderStepReset;
    instance->decoder.te_last = 0;
    instance->sync_count = 0;
    instance->raw_data = 0;
    instance->bit_count = 0;
    instance->generic.data = 0;
    instance->generic.data_count_bit = 0;
}

void subghz_protocol_decoder_porsche_touareg_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderPorscheTouareg* instance = context;

    const uint32_t te_short = subghz_protocol_porsche_touareg_const.te_short;
    const uint32_t te_long = subghz_protocol_porsche_touareg_const.te_long;
    const uint32_t te_delta = subghz_protocol_porsche_touareg_const.te_delta;

    switch(instance->decoder.parser_step) {
    case PorscheTouaregDecoderStepReset:
        if(!level && DURATION_DIFF(duration, PC_TE_SYNC) < te_delta) {
            instance->sync_count = 1;
            instance->decoder.parser_step = PorscheTouaregDecoderStepSync;
        }
        break;

    case PorscheTouaregDecoderStepSync:
        if(level) {
            if(DURATION_DIFF(duration, PC_TE_SYNC) < te_delta) {
                // keep collecting sync pairs
            } else if(
                instance->sync_count >= PC_SYNC_MIN &&
                DURATION_DIFF(duration, PC_TE_GAP) < te_delta) {
                instance->decoder.parser_step = PorscheTouaregDecoderStepGapLow;
            } else {
                instance->decoder.parser_step = PorscheTouaregDecoderStepReset;
            }
        } else {
            if(DURATION_DIFF(duration, PC_TE_SYNC) < te_delta) {
                instance->sync_count++;
            } else if(
                instance->sync_count >= PC_SYNC_MIN &&
                DURATION_DIFF(duration, PC_TE_GAP) < te_delta) {
                instance->decoder.parser_step = PorscheTouaregDecoderStepGapHigh;
            } else {
                instance->decoder.parser_step = PorscheTouaregDecoderStepReset;
            }
        }
        break;

    case PorscheTouaregDecoderStepGapHigh:
        if(level && DURATION_DIFF(duration, PC_TE_GAP) < te_delta) {
            instance->raw_data = 0;
            instance->bit_count = 0;
            instance->decoder.parser_step = PorscheTouaregDecoderStepData;
        } else {
            instance->decoder.parser_step = PorscheTouaregDecoderStepReset;
        }
        break;

    case PorscheTouaregDecoderStepGapLow:
        if(!level && DURATION_DIFF(duration, PC_TE_GAP) < te_delta) {
            instance->raw_data = 0;
            instance->bit_count = 0;
            instance->decoder.parser_step = PorscheTouaregDecoderStepData;
        } else {
            instance->decoder.parser_step = PorscheTouaregDecoderStepReset;
        }
        break;

    case PorscheTouaregDecoderStepData:
        if(level) {
            bool bit_value = false;
            if(DURATION_DIFF(instance->decoder.te_last, te_short) < te_delta &&
               DURATION_DIFF(duration, te_long) < te_delta) {
                bit_value = false;
            } else if(
                DURATION_DIFF(instance->decoder.te_last, te_long) < te_delta &&
                DURATION_DIFF(duration, te_short) < te_delta) {
                bit_value = true;
            } else {
                instance->decoder.parser_step = PorscheTouaregDecoderStepReset;
                break;
            }

            instance->raw_data = (instance->raw_data << 1) | (bit_value ? 1U : 0U);
            instance->bit_count++;

            if(instance->bit_count >= PORSCHE_TOUAREG_BIT_COUNT) {
                porsche_touareg_publish_frame(instance);
                instance->decoder.parser_step = PorscheTouaregDecoderStepReset;
            }
        } else {
            instance->decoder.te_last = duration;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_porsche_touareg_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderPorscheTouareg* instance = context;
    SubGhzBlockDecoder decoder = {
        .decode_data = instance->generic.data,
        .decode_count_bit = instance->generic.data_count_bit,
    };
    return subghz_protocol_blocks_get_hash_data(&decoder, (decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_porsche_touareg_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderPorscheTouareg* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret == SubGhzProtocolStatusOk) {
        ppa_serialize_fields(
            flipper_format,
            PP_FIELD_SERIAL | PP_FIELD_BTN | PP_FIELD_CNT,
            instance->generic.serial & 0xFFFFFF,
            instance->generic.btn,
            instance->generic.cnt,
            0);
    }

    return ret;
}

SubGhzProtocolStatus subghz_protocol_decoder_porsche_touareg_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderPorscheTouareg* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_porsche_touareg_const.min_count_bit_for_found);

    if(ret == SubGhzProtocolStatusOk) {
        porsche_touareg_parse_data(instance);

        uint32_t serial = 0;
        if(flipper_format_read_uint32(flipper_format, FF_SERIAL, &serial, 1)) {
            instance->generic.serial = serial & 0xFFFFFF;
        }

        uint32_t cnt = 0;
        if(flipper_format_read_uint32(flipper_format, FF_CNT, &cnt, 1)) {
            instance->generic.cnt = cnt;
        }

        uint32_t btn = 0;
        if(flipper_format_read_uint32(flipper_format, FF_BTN, &btn, 1)) {
            instance->generic.btn = (uint8_t)btn;
        }
    }

    return ret;
}

