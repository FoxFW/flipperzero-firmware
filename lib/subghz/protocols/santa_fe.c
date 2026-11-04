#include "santa_fe.h"

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
#include "common.h"
#include "../encode_plugin.h"
#include "pp_auto_common.h"


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


// Ported from KAT src/protocols/santa_fe.rs
#define SF_TE_SHORT    125
#define SF_TE_LONG     375
#define SF_TE_DELTA    100
#define SF_SYNC_US     12000
#define SF_SYNC_DELTA  1800
#define SF_GAP_US      15000
#define SF_BIT_COUNT   80
#define SF_BYTES       10

static const SubGhzBlockConst subghz_protocol_santa_fe_const = {
    .te_short = SF_TE_SHORT,
    .te_long = SF_TE_LONG,
    .te_delta = SF_TE_DELTA,
    .min_count_bit_for_found = SF_BIT_COUNT,
};

#define SF_UPLOAD_CAPACITY ((2U) + (2U * SF_BIT_COUNT) + 2U)
_Static_assert(
    SF_UPLOAD_CAPACITY <= PP_SHARED_UPLOAD_CAPACITY,
    "SF_UPLOAD_CAPACITY exceeds shared upload slab");

typedef enum {
    SfDecoderStepReset = 0,
    SfDecoderStepCheckSyncHi,
    SfDecoderStepSaveDuration,
    SfDecoderStepCheckDuration,
} SfDecoderStep;

struct SubGhzProtocolDecoderSantaFe {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t data[SF_BYTES];
    uint16_t bit_count;
    uint32_t rolling;
};

struct SubGhzProtocolEncoderSantaFe {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    uint32_t rolling;
};

static uint8_t sf_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0xFF;
    for(size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for(int b = 0; b < 8; b++) {
            if(crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0x31);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

static void sf_add_bit(SubGhzProtocolDecoderSantaFe* instance, bool bit) {
    if(instance->bit_count < SF_BIT_COUNT) {
        uint16_t byte_idx = instance->bit_count / 8;
        uint8_t bit_idx = 7 - (instance->bit_count % 8);
        if(bit) instance->data[byte_idx] |= (uint8_t)(1U << bit_idx);
        instance->bit_count++;
    }
}

static void sf_process(SubGhzProtocolDecoderSantaFe* instance) {
    if(instance->bit_count < SF_BIT_COUNT) return;
    if(instance->data[9] != sf_crc8(instance->data, 9)) return;

    uint32_t rolling = ((uint32_t)instance->data[0] << 24) | ((uint32_t)instance->data[1] << 16) |
                       ((uint32_t)instance->data[2] << 8) | instance->data[3];
    uint32_t serial = ((uint32_t)instance->data[4] << 16) | ((uint32_t)instance->data[5] << 8) |
                      instance->data[6];
    uint8_t counter = instance->data[7];
    uint8_t button = instance->data[8];

    instance->rolling = rolling;
    // 64-bit hash projection: rolling<<32 | serial<<8 | counter (KAT `packed` bytes 0..7)
    instance->generic.data = ((uint64_t)rolling << 32) | ((uint64_t)serial << 8) | counter;
    instance->generic.data_count_bit = SF_BIT_COUNT;
    instance->generic.serial = serial;
    instance->generic.btn = button;
    instance->generic.cnt = counter;

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

void* subghz_protocol_decoder_santa_fe_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderSantaFe* instance = calloc(1, sizeof(SubGhzProtocolDecoderSantaFe));
    furi_check(instance);
    instance->base.protocol = &santa_fe_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_santa_fe_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderSantaFe* instance = context;
    instance->decoder.parser_step = SfDecoderStepReset;
    memset(instance->data, 0, sizeof(instance->data));
    instance->bit_count = 0;
}

void subghz_protocol_decoder_santa_fe_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderSantaFe* instance = context;
    const uint32_t te_short = SF_TE_SHORT;
    const uint32_t te_long = SF_TE_LONG;
    const uint32_t te_delta = SF_TE_DELTA;

    switch(instance->decoder.parser_step) {
    case SfDecoderStepReset:
        if(level && DURATION_DIFF(duration, te_long) < te_delta) {
            instance->decoder.parser_step = SfDecoderStepCheckSyncHi;
        }
        break;

    case SfDecoderStepCheckSyncHi:
        if(!level && DURATION_DIFF(duration, SF_SYNC_US) < SF_SYNC_DELTA) {
            instance->decoder.parser_step = SfDecoderStepSaveDuration;
            memset(instance->data, 0, sizeof(instance->data));
            instance->bit_count = 0;
        } else {
            instance->decoder.parser_step = SfDecoderStepReset;
        }
        break;

    case SfDecoderStepSaveDuration:
        if(level) {
            if(DURATION_DIFF(duration, te_long) < te_delta) {
                sf_add_bit(instance, true);
                instance->decoder.parser_step = SfDecoderStepCheckDuration;
            } else if(DURATION_DIFF(duration, te_short) < te_delta) {
                sf_add_bit(instance, false);
                instance->decoder.parser_step = SfDecoderStepCheckDuration;
            } else if(duration > 3000) {
                if(instance->bit_count >= SF_BIT_COUNT) sf_process(instance);
                instance->decoder.parser_step = SfDecoderStepReset;
            } else {
                instance->decoder.parser_step = SfDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = SfDecoderStepReset;
        }
        break;

    case SfDecoderStepCheckDuration:
        if(!level) {
            if(DURATION_DIFF(duration, te_short) < te_delta ||
               DURATION_DIFF(duration, te_long) < te_delta) {
                if(instance->bit_count >= SF_BIT_COUNT) {
                    sf_process(instance);
                    instance->decoder.parser_step = SfDecoderStepReset;
                } else {
                    instance->decoder.parser_step = SfDecoderStepSaveDuration;
                }
            } else if(duration > 3000) {
                if(instance->bit_count >= SF_BIT_COUNT) sf_process(instance);
                instance->decoder.parser_step = SfDecoderStepReset;
            } else {
                instance->decoder.parser_step = SfDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = SfDecoderStepReset;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_santa_fe_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderSantaFe* instance = context;
    SubGhzBlockDecoder decoder = {
        .decode_data = instance->generic.data,
        .decode_count_bit = instance->generic.data_count_bit,
    };
    return subghz_protocol_blocks_get_hash_data(&decoder, (decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_santa_fe_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderSantaFe* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret == SubGhzProtocolStatusOk) {
        ppa_serialize_fields(
            flipper_format,
            PP_FIELD_SERIAL | PP_FIELD_BTN | PP_FIELD_CNT,
            instance->generic.serial,
            instance->generic.btn,
            instance->generic.cnt,
            0);
        uint32_t rolling = instance->rolling;
        flipper_format_write_uint32(flipper_format, "Rolling", &rolling, 1);
    }
    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_santa_fe_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderSantaFe* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_santa_fe_const.min_count_bit_for_found);
    if(ret == SubGhzProtocolStatusOk) {
        instance->generic.serial = (uint32_t)((instance->generic.data >> 8) & 0xFFFFFF);
        instance->generic.cnt = (uint16_t)(instance->generic.data & 0xFF);
        instance->rolling = (uint32_t)(instance->generic.data >> 32);
        uint32_t rolling = instance->rolling;
        if(flipper_format_read_uint32(flipper_format, "Rolling", &rolling, 1)) {
            instance->rolling = rolling;
        }
    }
    return ret;
}

const SubGhzProtocolDecoder subghz_protocol_santa_fe_decoder = {
    .alloc = subghz_protocol_decoder_santa_fe_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_santa_fe_feed,
    .reset = subghz_protocol_decoder_santa_fe_reset,
    .get_hash_data = subghz_protocol_decoder_santa_fe_get_hash_data,
    .serialize = subghz_protocol_decoder_santa_fe_serialize,
    .deserialize = subghz_protocol_decoder_santa_fe_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

/* host bridge: firmware functions/state the SD encoder uses (handed over at load) */
typedef struct {
    __typeof__(&ppa_emit) ppa_emit;
    __typeof__(&ppa_encoder_buffer_ensure) ppa_encoder_buffer_ensure;
    __typeof__(&ppa_encoder_free) ppa_encoder_free;
    __typeof__(&ppa_encoder_read_fields) ppa_encoder_read_fields;
    __typeof__(&ppa_encoder_read_repeat) ppa_encoder_read_repeat;
    __typeof__(&ppa_encoder_stop) ppa_encoder_stop;
    __typeof__(&ppa_encoder_yield) ppa_encoder_yield;
    __typeof__(&ppa_verify_protocol_name) ppa_verify_protocol_name;
} enc_santa_fe_host_t;
static const enc_santa_fe_host_t enc_santa_fe_host = {
    .ppa_emit = ppa_emit,
    .ppa_encoder_buffer_ensure = ppa_encoder_buffer_ensure,
    .ppa_encoder_free = ppa_encoder_free,
    .ppa_encoder_read_fields = ppa_encoder_read_fields,
    .ppa_encoder_read_repeat = ppa_encoder_read_repeat,
    .ppa_encoder_stop = ppa_encoder_stop,
    .ppa_encoder_yield = ppa_encoder_yield,
    .ppa_verify_protocol_name = ppa_verify_protocol_name,
};
static const SubGhzEncoderExternal subghz_protocol_santa_fe_encoder_ext = SUBGHZ_ENCODER_EXTERNAL_HOST("enc_santa_fe", &enc_santa_fe_host);

const SubGhzProtocol santa_fe_protocol = {
    .name = SANTA_FE_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_santa_fe_decoder,
    .encoder = &subghz_protocol_santa_fe_encoder_ext.vtable,
};
