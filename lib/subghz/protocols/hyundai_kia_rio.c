#include "hyundai_kia_rio.h"

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


// Ported from KAT src/protocols/hyundai_kia_rio.rs
#define HKR_BIT_COUNT 64
#define HKR_TE_SHORT  312
#define HKR_TE_LONG   728
#define HKR_TE_DELTA  150
#define HKR_SYNC_US   10400
#define HKR_SYNC_DELTA 1560
#define HKR_GAP_US    10000

static const SubGhzBlockConst subghz_protocol_hyundai_kia_rio_const = {
    .te_short = HKR_TE_SHORT,
    .te_long = HKR_TE_LONG,
    .te_delta = HKR_TE_DELTA,
    .min_count_bit_for_found = HKR_BIT_COUNT,
};

#define HKR_UPLOAD_CAPACITY ((2U) + (2U * HKR_BIT_COUNT) + 2U)
_Static_assert(
    HKR_UPLOAD_CAPACITY <= PP_SHARED_UPLOAD_CAPACITY,
    "HKR_UPLOAD_CAPACITY exceeds shared upload slab");

typedef enum {
    HkrDecoderStepReset = 0,
    HkrDecoderStepCheckSync,
    HkrDecoderStepSaveDuration,
    HkrDecoderStepCheckDuration,
} HkrDecoderStep;

struct SubGhzProtocolDecoderHyundaiKiaRio {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint64_t data;
    uint8_t bit_count;
};

struct SubGhzProtocolEncoderHyundaiKiaRio {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static uint8_t hkr_mask_to_button(uint16_t mask) {
    switch(mask) {
    case 0x0100:
        return 1;
    case 0x0200:
        return 2;
    case 0x0400:
        return 4;
    case 0x0800:
        return 8;
    default:
        return (uint8_t)(mask >> 8);
    }
}

static uint16_t hkr_checksum(uint32_t serial, uint16_t mask) {
    uint16_t c = (uint16_t)(serial ^ (serial >> 16));
    c ^= mask;
    return (uint16_t)(~c);
}

static void hkr_publish(SubGhzProtocolDecoderHyundaiKiaRio* instance) {
    uint32_t serial = (uint32_t)(instance->data >> 32);
    uint16_t mask = (uint16_t)((instance->data >> 16) & 0xFFFF);
    uint16_t rx_ck = (uint16_t)(instance->data & 0xFFFF);

    if(rx_ck != hkr_checksum(serial, mask)) {
        return;
    }

    instance->generic.data = instance->data;
    instance->generic.data_count_bit = HKR_BIT_COUNT;
    instance->generic.serial = serial;
    instance->generic.btn = hkr_mask_to_button(mask);
    instance->generic.cnt = 0;

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

void* subghz_protocol_decoder_hyundai_kia_rio_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderHyundaiKiaRio* instance =
        calloc(1, sizeof(SubGhzProtocolDecoderHyundaiKiaRio));
    furi_check(instance);
    instance->base.protocol = &hyundai_kia_rio_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_hyundai_kia_rio_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderHyundaiKiaRio* instance = context;
    instance->decoder.parser_step = HkrDecoderStepReset;
    instance->data = 0;
    instance->bit_count = 0;
    instance->generic.data = 0;
    instance->generic.data_count_bit = 0;
}

static void hkr_add_bit(SubGhzProtocolDecoderHyundaiKiaRio* instance, bool bit) {
    instance->data = (instance->data << 1) | (bit ? 1U : 0U);
    instance->bit_count++;
}

void subghz_protocol_decoder_hyundai_kia_rio_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderHyundaiKiaRio* instance = context;

    const uint32_t te_short = subghz_protocol_hyundai_kia_rio_const.te_short;
    const uint32_t te_long = subghz_protocol_hyundai_kia_rio_const.te_long;
    const uint32_t te_delta = subghz_protocol_hyundai_kia_rio_const.te_delta;

    switch(instance->decoder.parser_step) {
    case HkrDecoderStepReset:
        if(level && DURATION_DIFF(duration, te_short) < te_delta) {
            instance->decoder.parser_step = HkrDecoderStepCheckSync;
        }
        break;

    case HkrDecoderStepCheckSync:
        if(!level && DURATION_DIFF(duration, HKR_SYNC_US) < HKR_SYNC_DELTA) {
            instance->decoder.parser_step = HkrDecoderStepSaveDuration;
            instance->data = 0;
            instance->bit_count = 0;
        } else {
            instance->decoder.parser_step = HkrDecoderStepReset;
        }
        break;

    case HkrDecoderStepSaveDuration:
        if(level) {
            if(DURATION_DIFF(duration, te_long) < te_delta) {
                hkr_add_bit(instance, true);
                instance->decoder.parser_step = HkrDecoderStepCheckDuration;
            } else if(DURATION_DIFF(duration, te_short) < te_delta) {
                hkr_add_bit(instance, false);
                instance->decoder.parser_step = HkrDecoderStepCheckDuration;
            } else if(duration > 3000) {
                if(instance->bit_count >= HKR_BIT_COUNT) {
                    hkr_publish(instance);
                }
                instance->decoder.parser_step = HkrDecoderStepReset;
            } else {
                instance->decoder.parser_step = HkrDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = HkrDecoderStepReset;
        }
        break;

    case HkrDecoderStepCheckDuration:
        if(!level) {
            if(DURATION_DIFF(duration, te_short) < te_delta ||
               DURATION_DIFF(duration, te_long) < te_delta) {
                if(instance->bit_count >= HKR_BIT_COUNT) {
                    hkr_publish(instance);
                    instance->decoder.parser_step = HkrDecoderStepReset;
                } else {
                    instance->decoder.parser_step = HkrDecoderStepSaveDuration;
                }
            } else if(duration > 3000) {
                if(instance->bit_count >= HKR_BIT_COUNT) {
                    hkr_publish(instance);
                }
                instance->decoder.parser_step = HkrDecoderStepReset;
            } else {
                instance->decoder.parser_step = HkrDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = HkrDecoderStepReset;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_hyundai_kia_rio_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderHyundaiKiaRio* instance = context;
    SubGhzBlockDecoder decoder = {
        .decode_data = instance->generic.data,
        .decode_count_bit = instance->generic.data_count_bit,
    };
    return subghz_protocol_blocks_get_hash_data(&decoder, (decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_hyundai_kia_rio_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderHyundaiKiaRio* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret == SubGhzProtocolStatusOk) {
        ppa_serialize_fields(
            flipper_format,
            PP_FIELD_SERIAL | PP_FIELD_BTN,
            instance->generic.serial,
            instance->generic.btn,
            0,
            0);
    }
    return ret;
}

SubGhzProtocolStatus subghz_protocol_decoder_hyundai_kia_rio_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderHyundaiKiaRio* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_hyundai_kia_rio_const.min_count_bit_for_found);
    if(ret == SubGhzProtocolStatusOk) {
        instance->generic.serial = (uint32_t)(instance->generic.data >> 32);
        instance->generic.btn =
            hkr_mask_to_button((uint16_t)((instance->generic.data >> 16) & 0xFFFF));
    }
    return ret;
}

const SubGhzProtocolDecoder subghz_protocol_hyundai_kia_rio_decoder = {
    .alloc = subghz_protocol_decoder_hyundai_kia_rio_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_hyundai_kia_rio_feed,
    .reset = subghz_protocol_decoder_hyundai_kia_rio_reset,
    .get_hash_data = subghz_protocol_decoder_hyundai_kia_rio_get_hash_data,
    .serialize = subghz_protocol_decoder_hyundai_kia_rio_serialize,
    .deserialize = subghz_protocol_decoder_hyundai_kia_rio_deserialize,
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
    __typeof__(&ppa_u64_to_bytes_be) ppa_u64_to_bytes_be;
    __typeof__(&ppa_verify_protocol_name) ppa_verify_protocol_name;
} enc_hyundai_kia_rio_host_t;
static const enc_hyundai_kia_rio_host_t enc_hyundai_kia_rio_host = {
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
static const SubGhzEncoderExternal subghz_protocol_hyundai_kia_rio_encoder_ext = SUBGHZ_ENCODER_EXTERNAL_HOST("enc_hyundai_kia_rio", &enc_hyundai_kia_rio_host);

const SubGhzProtocol hyundai_kia_rio_protocol = {
    .name = HYUNDAI_KIA_RIO_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_hyundai_kia_rio_decoder,
    .encoder = &subghz_protocol_hyundai_kia_rio_encoder_ext.vtable,
};
