#include "bmw_v4.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/common.h>
#include "../encode_plugin.h"

#define BMW_TE_US             62
#define BMW_TE1_MIN_US        40
#define BMW_TE1_MAX_US        90
#define BMW_TE2_MIN_US        100
#define BMW_TE2_MAX_US        160
#define BMW_GAP_US            400
#define BMW_PREAMBLE_MIN      32
#define BMW_PREAMBLE_MAX      280
#define BMW_PREAMBLE_BITS     212
#define BMW_SYNC              0x599EU
#define BMW_SYNC_INV          0xA661U
#define BMW_HEADER_HUNT_MAX   40
#define BMW_PAYLOAD_BITS      128
#define BMW_FIXED_VALUE       0x89U
#define BMW_CNT_BITS          17
#define BMW_CNT_MASK          0x1FFFFU
#define BMW_HOP_MASK          0x7FFFFFFFFFFFULL
#define BMW_BTN_TEMPLATE      0x01U
#define BMW_BTN_TEMPLATE_MASK 0xE7U
#define BMW_BTN_XX_MASK       0x3U
#define BMW_BTN_UNLOCK        0x1U
#define BMW_BTN_LOCK          0x2U
#define BMW_BTN_TRUNK         0x3U
#define BMW_BTN_MAX           3
#define BMW_REPEAT_GAP_US     200000
#define BMW_REPEAT_DEFAULT    3
#define BMW_UPLOAD_CAPACITY   2048

static const SubGhzBlockConst subghz_protocol_bmw_v4_const = {
    .te_short = BMW_TE_US,
    .te_long = BMW_TE_US * 2,
    .te_delta = 25,
    .min_count_bit_for_found = BMW_PAYLOAD_BITS,
};

typedef enum {
    BMWDecoderStepReset = 0,
    BMWDecoderStepAgc,
    BMWDecoderStepWaitPreambleHigh,
    BMWDecoderStepPreambleLow,
    BMWDecoderStepPreambleHigh,
    BMWDecoderStepHeader,
    BMWDecoderStepData,
} BMWDecoderStep;

struct SubGhzProtocolDecoderBmwV4 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint64_t decode_head;
    uint16_t preamble_count;
    uint16_t header_bits;
    uint16_t sync_reg;
    int8_t pair_chip;
    bool invert;
};

static bool bmw_v4_is_1t(uint32_t duration) {
    return (duration >= BMW_TE1_MIN_US) && (duration <= BMW_TE1_MAX_US);
}

static bool bmw_v4_is_2t(uint32_t duration) {
    return (duration >= BMW_TE2_MIN_US) && (duration <= BMW_TE2_MAX_US);
}

static uint8_t bmw_v4_chips(uint32_t duration) {
    if(bmw_v4_is_1t(duration)) return 1;
    if(bmw_v4_is_2t(duration)) return 2;
    return 0;
}

static void bmw_v4_normalize_stored_payload(SubGhzBlockGeneric* instance) {
    if((uint16_t)(instance->data >> 48) == BMW_SYNC_INV) {
        instance->data = ~instance->data;
        instance->data_2 = ~instance->data_2;
    }
}

static uint8_t bmw_v4_wire_xx(uint8_t btn_byte) {
    return (uint8_t)((btn_byte >> 3) & BMW_BTN_XX_MASK);
}

static uint32_t bmw_v4_wire_cnt(uint64_t lo) {
    return (uint32_t)((((lo >> 48) & 0xFFFFU) << 1) | ((lo >> 47) & 1U));
}

static void bmw_v4_parse_fields(SubGhzBlockGeneric* instance) {
    const uint64_t hi = instance->data;
    const uint64_t lo = instance->data_2;
    const uint8_t btn_byte = (uint8_t)((hi >> 8) & 0xFFU);

    instance->serial = (uint32_t)(hi >> 16);
    instance->btn = bmw_v4_wire_xx(btn_byte);
    instance->cnt = bmw_v4_wire_cnt(lo) & BMW_CNT_MASK;
}

static bool bmw_v4_fields_valid(const SubGhzBlockGeneric* instance) {
    const uint64_t hi = instance->data;
    const uint16_t sync = (uint16_t)(hi >> 48);
    const uint8_t btn_byte = (uint8_t)((hi >> 8) & 0xFFU);
    const uint8_t fixed = (uint8_t)(hi & 0xFFULL);

    if(sync != BMW_SYNC) return false;
    if((btn_byte & BMW_BTN_TEMPLATE_MASK) != BMW_BTN_TEMPLATE) return false;
    if(bmw_v4_wire_xx(btn_byte) == 0) return false;
    if(fixed != BMW_FIXED_VALUE) return false;
    return true;
}

static uint8_t bmw_v4_custom_id_from_code(uint8_t code) {
    switch(code) {
    case BMW_BTN_LOCK: return SUBGHZ_CUSTOM_BTN_UP;
    case BMW_BTN_UNLOCK: return SUBGHZ_CUSTOM_BTN_DOWN;
    case BMW_BTN_TRUNK: return SUBGHZ_CUSTOM_BTN_LEFT;
    default: return SUBGHZ_CUSTOM_BTN_OK;
    }
}

void* subghz_protocol_decoder_bmw_v4_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderBmwV4* instance = malloc(sizeof(SubGhzProtocolDecoderBmwV4));
    instance->base.protocol = &subghz_protocol_bmw_v4;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->pair_chip = -1;
    return instance;
}

void subghz_protocol_decoder_bmw_v4_free(void* context) {
    furi_check(context);
    free(context);
}

static void bmw_v4_reset_soft(SubGhzProtocolDecoderBmwV4* instance) {
    instance->decoder.parser_step = BMWDecoderStepReset;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->decode_head = 0;
    instance->preamble_count = 0;
    instance->header_bits = 0;
    instance->sync_reg = 0;
    instance->pair_chip = -1;
    instance->invert = false;
}

void subghz_protocol_decoder_bmw_v4_reset(void* context) {
    furi_check(context);
    bmw_v4_reset_soft(context);
}

static void bmw_v4_try_callback(SubGhzProtocolDecoderBmwV4* instance) {
    if(instance->decoder.decode_count_bit != subghz_protocol_bmw_v4_const.min_count_bit_for_found)
        return;
    instance->generic.data = instance->decode_head;
    instance->generic.data_2 = instance->decoder.decode_data;
    instance->generic.data_count_bit = BMW_PAYLOAD_BITS;
    if(!bmw_v4_fields_valid(&instance->generic)) {
        bmw_v4_reset_soft(instance);
        return;
    }
    bmw_v4_parse_fields(&instance->generic);
    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(bmw_v4_custom_id_from_code(instance->generic.btn));
    }
    subghz_custom_btn_set_max(BMW_BTN_MAX);
    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
    bmw_v4_reset_soft(instance);
}

static void bmw_v4_seed_payload_sync(SubGhzProtocolDecoderBmwV4* instance) {
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->decode_head = 0;
    for(int8_t i = 15; i >= 0; i--) {
        subghz_protocol_blocks_add_to_128_bit(
            &instance->decoder, (BMW_SYNC >> i) & 1, &instance->decode_head);
    }
}

static void bmw_v4_add_logical_bit(SubGhzProtocolDecoderBmwV4* instance, bool bit) {
    switch(instance->decoder.parser_step) {
    case BMWDecoderStepHeader:
        instance->sync_reg = (uint16_t)((instance->sync_reg << 1) | (bit ? 1U : 0U));
        instance->header_bits++;
        if(instance->sync_reg == BMW_SYNC) {
            instance->invert = false;
            bmw_v4_seed_payload_sync(instance);
            instance->decoder.parser_step = BMWDecoderStepData;
        } else if(instance->sync_reg == BMW_SYNC_INV) {
            instance->invert = true;
            bmw_v4_seed_payload_sync(instance);
            instance->decoder.parser_step = BMWDecoderStepData;
        } else if(instance->header_bits >= BMW_HEADER_HUNT_MAX) {
            bmw_v4_reset_soft(instance);
        }
        break;
    case BMWDecoderStepData: {
        bool value = instance->invert ? !bit : bit;
        if(instance->decoder.decode_count_bit >=
           subghz_protocol_bmw_v4_const.min_count_bit_for_found) {
            bmw_v4_try_callback(instance);
            return;
        }
        subghz_protocol_blocks_add_to_128_bit(
            &instance->decoder, value ? 1 : 0, &instance->decode_head);
        if(instance->decoder.decode_count_bit ==
           subghz_protocol_bmw_v4_const.min_count_bit_for_found) {
            bmw_v4_try_callback(instance);
        }
        break;
    }
    default:
        bmw_v4_reset_soft(instance);
        break;
    }
}

static void bmw_v4_add_chip(SubGhzProtocolDecoderBmwV4* instance, bool chip) {
    if(instance->pair_chip < 0) {
        instance->pair_chip = chip ? 1 : 0;
        return;
    }
    const uint8_t first = (uint8_t)instance->pair_chip;
    const uint8_t second = chip ? 1 : 0;
    instance->pair_chip = -1;
    if(first == 0 && second == 1) {
        bmw_v4_add_logical_bit(instance, true);
    } else if(first == 1 && second == 0) {
        bmw_v4_add_logical_bit(instance, false);
    } else {
        bmw_v4_reset_soft(instance);
    }
}

static void bmw_v4_enter_header(SubGhzProtocolDecoderBmwV4* instance, bool level) {
    instance->decoder.parser_step = BMWDecoderStepHeader;
    instance->header_bits = 0;
    instance->sync_reg = 0;
    instance->invert = false;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->decode_head = 0;
    instance->pair_chip = level ? 1 : 0;
}

static void bmw_v4_finish_gap(SubGhzProtocolDecoderBmwV4* instance, bool level) {
    if(instance->decoder.parser_step == BMWDecoderStepHeader ||
       instance->decoder.parser_step == BMWDecoderStepData) {
        if(instance->pair_chip == 0 && level) {
            bmw_v4_add_logical_bit(instance, true);
        } else if(instance->pair_chip == 1 && !level) {
            bmw_v4_add_logical_bit(instance, false);
        }
        instance->pair_chip = -1;
        bmw_v4_try_callback(instance);
    }
    bmw_v4_reset_soft(instance);
}

void subghz_protocol_decoder_bmw_v4_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderBmwV4* instance = context;

    if(duration >= BMW_GAP_US) {
        bmw_v4_finish_gap(instance, level);
        return;
    }

    const uint8_t n = bmw_v4_chips(duration);
    if(n == 0) {
        bmw_v4_reset_soft(instance);
        return;
    }

    switch(instance->decoder.parser_step) {
    case BMWDecoderStepReset:
        if(level && n == 2) {
            instance->decoder.parser_step = BMWDecoderStepAgc;
        } else if(level && n == 1) {
            instance->preamble_count = 0;
            instance->decoder.parser_step = BMWDecoderStepPreambleLow;
        } else if(!level && n == 1) {
            instance->preamble_count = 0;
            instance->decoder.parser_step = BMWDecoderStepPreambleHigh;
        }
        break;

    case BMWDecoderStepAgc:
        if(!level && (n == 1 || n == 2)) {
            instance->decoder.parser_step = BMWDecoderStepWaitPreambleHigh;
        } else if(level && n == 1) {
            instance->preamble_count = 0;
            instance->decoder.parser_step = BMWDecoderStepPreambleLow;
        } else {
            bmw_v4_reset_soft(instance);
        }
        break;

    case BMWDecoderStepWaitPreambleHigh:
        if(level && n == 1) {
            instance->preamble_count = 0;
            instance->decoder.parser_step = BMWDecoderStepPreambleLow;
        } else if(level && n == 2) {
            instance->decoder.parser_step = BMWDecoderStepAgc;
        } else if(!level && (n == 1 || n == 2)) {
        } else {
            bmw_v4_reset_soft(instance);
        }
        break;

    case BMWDecoderStepPreambleLow:
        if(!level && n == 1) {
            instance->preamble_count++;
            if(instance->preamble_count > BMW_PREAMBLE_MAX) {
                bmw_v4_reset_soft(instance);
                break;
            }
            instance->decoder.parser_step = BMWDecoderStepPreambleHigh;
        } else if(!level && n == 2 && instance->preamble_count >= BMW_PREAMBLE_MIN) {
            bmw_v4_enter_header(instance, false);
        } else {
            bmw_v4_reset_soft(instance);
        }
        break;

    case BMWDecoderStepPreambleHigh:
        if(level && n == 1) {
            instance->decoder.parser_step = BMWDecoderStepPreambleLow;
        } else if(level && n == 2 && instance->preamble_count >= BMW_PREAMBLE_MIN) {
            bmw_v4_enter_header(instance, true);
        } else {
            bmw_v4_reset_soft(instance);
        }
        break;

    case BMWDecoderStepHeader:
    case BMWDecoderStepData:
        for(uint8_t i = 0; i < n; i++) {
            if(instance->decoder.parser_step != BMWDecoderStepHeader &&
               instance->decoder.parser_step != BMWDecoderStepData) {
                break;
            }
            bmw_v4_add_chip(instance, level);
        }
        break;

    default:
        bmw_v4_reset_soft(instance);
        break;
    }

    instance->decoder.te_last = duration;
}

uint8_t subghz_protocol_decoder_bmw_v4_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderBmwV4* instance = context;
    uint64_t mix = instance->generic.data ^ instance->generic.data_2;
    return (uint8_t)(mix ^ (mix >> 8) ^ (mix >> 16) ^ (mix >> 24) ^ (mix >> 32) ^ (mix >> 40));
}

SubGhzProtocolStatus
    subghz_protocol_decoder_bmw_v4_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderBmwV4* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, BMW_PAYLOAD_BITS);
    if(ret == SubGhzProtocolStatusOk) {
        instance->generic.data_2 = 0;
        uint8_t key_data[sizeof(uint64_t)] = {0};
        if(flipper_format_rewind(flipper_format) &&
           flipper_format_read_hex(flipper_format, "Data", key_data, sizeof(uint64_t))) {
            for(uint8_t i = 0; i < sizeof(uint64_t); i++)
                instance->generic.data_2 = (instance->generic.data_2 << 8) | key_data[i];
        }
        bmw_v4_normalize_stored_payload(&instance->generic);
        bmw_v4_parse_fields(&instance->generic);
    }
    return ret;
}

const SubGhzProtocolDecoder subghz_protocol_bmw_v4_decoder = {
    .alloc = subghz_protocol_decoder_bmw_v4_alloc,
    .free = subghz_protocol_decoder_bmw_v4_free,
    .feed = subghz_protocol_decoder_bmw_v4_feed,
    .reset = subghz_protocol_decoder_bmw_v4_reset,
    .get_hash_data = subghz_protocol_decoder_bmw_v4_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize_data_2,
    .deserialize = subghz_protocol_decoder_bmw_v4_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_bmw_v4_encoder_ext =
    SUBGHZ_ENCODER_EXTERNAL("enc_bmw_v4");

const SubGhzProtocol subghz_protocol_bmw_v4 = {
    .name = SUBGHZ_PROTOCOL_BMW_V4_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_bmw_v4_decoder,
    .encoder = &subghz_protocol_bmw_v4_encoder_ext.vtable,
};
