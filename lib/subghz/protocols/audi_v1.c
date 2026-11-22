

#include "audi_v1.h"

#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#include <lib/subghz/blocks/custom_btn_i.h>

#include <furi.h>
#include <furi_hal.h>
#include "common.h"
#include "../encode_plugin.h"

#define TAG "SubGhzProtocolAudiV1"

#define AUDI_V1_TE_SYNC         3000
#define AUDI_V1_TE_HEADER       2000
#define AUDI_V1_TE_SYNC_DELTA   800
#define AUDI_V1_TE_HEADER_DELTA 500
#define AUDI_V1_DATA_BITS       88
#define AUDI_V1_PREAMBLE_COUNT  20

#define AUDI_V1_BTN_LOCK   0x0E
#define AUDI_V1_BTN_UNLOCK 0x0B

static const SubGhzBlockConst subghz_protocol_audi_v1_const = {
    .te_short = 260,
    .te_long = 520,
    .te_delta = 130,
    .min_count_bit_for_found = 88,
};

typedef enum {
    AudiV1DecoderStepReset = 0,
    AudiV1DecoderStepWaitHeader,
    AudiV1DecoderStepPreamble,
    AudiV1DecoderStepData,
} AudiV1DecoderStep;

struct SubGhzProtocolDecoderAudiV1 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t preamble_count;
    ManchesterState manchester_saved_state;
    uint64_t hop;
    uint8_t btn_byte;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderAudiV1);

struct SubGhzProtocolEncoderAudiV1 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint64_t hop;
    uint8_t btn_byte;
    uint32_t serial;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderAudiV1);

const SubGhzProtocolDecoder subghz_protocol_audi_v1_decoder = {
    .alloc = subghz_protocol_decoder_audi_v1_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_audi_v1_feed,
    .reset = subghz_protocol_decoder_audi_v1_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_audi_v1_serialize,
    .deserialize = subghz_protocol_decoder_audi_v1_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_audi_v1_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_audi_v1");

const SubGhzProtocol subghz_protocol_audi_v1 = {
    .name = SUBGHZ_PROTOCOL_AUDI_V1_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_audi_v1_decoder,
    .encoder = &subghz_protocol_audi_v1_encoder_ext.vtable,
};

static uint8_t audi_v1_extract_counter(uint64_t hop, uint8_t btn_byte) {
    uint8_t hop_bits = (hop >> 42) & 0x3F;
    uint8_t btn_lo = btn_byte & 0x03;
    return hop_bits | (btn_lo << 6);
}

static const char* audi_v1_get_button_name(uint8_t btn_id) {
    switch(btn_id) {
    case AUDI_V1_BTN_LOCK:
        return "Lock";
    case AUDI_V1_BTN_UNLOCK:
        return "Unlock";
    default:
        return "Unknown";
    }
}

static void audi_v1_extract_fields(SubGhzBlockGeneric* generic, uint64_t hop, uint8_t btn_byte) {
    generic->serial = (uint32_t)(generic->data >> 32);
    generic->btn = (btn_byte >> 4) & 0x0F;
    generic->cnt = audi_v1_extract_counter(hop, btn_byte);
}

void* subghz_protocol_decoder_audi_v1_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderAudiV1* instance = malloc(sizeof(SubGhzProtocolDecoderAudiV1));
    instance->base.protocol = &subghz_protocol_audi_v1;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->manchester_saved_state = ManchesterStateMid1;
    return instance;
}

void subghz_protocol_decoder_audi_v1_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderAudiV1* instance = context;
    instance->decoder.parser_step = AudiV1DecoderStepReset;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->preamble_count = 0;
    manchester_advance(
        instance->manchester_saved_state,
        ManchesterEventReset,
        &instance->manchester_saved_state,
        NULL);
}

static void audi_v1_decoder_check_complete(SubGhzProtocolDecoderAudiV1* instance) {
    if(instance->decoder.decode_count_bit < AUDI_V1_DATA_BITS) return;

    uint32_t remaining_24 = instance->decoder.decode_data & 0xFFFFFF;

    instance->btn_byte = (instance->generic.data >> 24) & 0xFF;
    instance->hop = ((instance->generic.data & 0xFFFFFFULL) << 24) | remaining_24;

    instance->generic.data_count_bit = AUDI_V1_DATA_BITS;
    audi_v1_extract_fields(&instance->generic, instance->hop, instance->btn_byte);

    uint8_t btn_id = (instance->btn_byte >> 4) & 0x0F;

    if((btn_id != AUDI_V1_BTN_LOCK && btn_id != AUDI_V1_BTN_UNLOCK) ||
       (instance->btn_byte & 0x0C) != 0 ||
       instance->generic.serial == 0) {
        instance->decoder.decode_data = 0;
        instance->decoder.decode_count_bit = 0;
        instance->decoder.parser_step = AudiV1DecoderStepReset;
        return;
    }

    subghz_block_generic_global.current_btn = btn_id;
    subghz_block_generic_global.btn_length_bit = 4;
    FURI_LOG_I(
        TAG,
        "Decoded 88-bit: Sn=%08lX Btn=%s(0x%02X) Cnt=%lu Hop=%012llX",
        (unsigned long)instance->generic.serial,
        audi_v1_get_button_name(btn_id),
        instance->btn_byte,
        (unsigned long)instance->generic.cnt,
        (unsigned long long)instance->hop);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }

    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->decoder.parser_step = AudiV1DecoderStepReset;
}

void subghz_protocol_decoder_audi_v1_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderAudiV1* instance = context;

    switch(instance->decoder.parser_step) {
    case AudiV1DecoderStepReset:

        if(!level && DURATION_DIFF(duration, AUDI_V1_TE_SYNC) <= AUDI_V1_TE_SYNC_DELTA) {
            instance->decoder.parser_step = AudiV1DecoderStepWaitHeader;
        }
        break;

    case AudiV1DecoderStepWaitHeader:

        if(level && DURATION_DIFF(duration, AUDI_V1_TE_HEADER) <= AUDI_V1_TE_HEADER_DELTA) {
            instance->decoder.parser_step = AudiV1DecoderStepPreamble;
            instance->preamble_count = 0;
        } else {
            instance->decoder.parser_step = AudiV1DecoderStepReset;
        }
        break;

    case AudiV1DecoderStepPreamble:

        if(DURATION_DIFF(duration, subghz_protocol_audi_v1_const.te_short) <=
           subghz_protocol_audi_v1_const.te_delta) {
            instance->preamble_count++;
        } else if(
            DURATION_DIFF(duration, subghz_protocol_audi_v1_const.te_long) <=
                subghz_protocol_audi_v1_const.te_delta &&
            instance->preamble_count >= AUDI_V1_PREAMBLE_COUNT) {
            instance->decoder.parser_step = AudiV1DecoderStepData;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->generic.data = 0;

            manchester_advance(
                instance->manchester_saved_state,
                ManchesterEventReset,
                &instance->manchester_saved_state,
                NULL);

            ManchesterEvent event = level ? ManchesterEventLongHigh : ManchesterEventLongLow;
            bool data;
            manchester_advance(
                instance->manchester_saved_state, event, &instance->manchester_saved_state, &data);
        } else {
            instance->decoder.parser_step = AudiV1DecoderStepReset;
        }
        break;

    case AudiV1DecoderStepData: {
        ManchesterEvent event = ManchesterEventReset;

        if(DURATION_DIFF(duration, subghz_protocol_audi_v1_const.te_short) <=
           subghz_protocol_audi_v1_const.te_delta) {
            event = level ? ManchesterEventShortHigh : ManchesterEventShortLow;
        } else if(
            DURATION_DIFF(duration, subghz_protocol_audi_v1_const.te_long) <=
            subghz_protocol_audi_v1_const.te_delta) {
            event = level ? ManchesterEventLongHigh : ManchesterEventLongLow;
        }

        if(event != ManchesterEventReset) {
            bool data;
            bool data_ok = manchester_advance(
                instance->manchester_saved_state, event, &instance->manchester_saved_state, &data);

            if(data_ok) {
                instance->decoder.decode_data = (instance->decoder.decode_data << 1) | data;
                instance->decoder.decode_count_bit++;

                if(instance->decoder.decode_count_bit == 64) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->decoder.decode_data = 0;
                }
            }
        }

        if(instance->decoder.decode_count_bit >= AUDI_V1_DATA_BITS) {
            audi_v1_decoder_check_complete(instance);
            break;
        }

        if(event == ManchesterEventReset) {
            if(instance->decoder.decode_count_bit >= 64 && instance->generic.data != 0) {
                FURI_LOG_W(
                    TAG,
                    "Partial frame: %lu bits (need 88)",
                    (unsigned long)instance->decoder.decode_count_bit);
            }
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = AudiV1DecoderStepReset;
        }
    } break;

    default:
        instance->decoder.parser_step = AudiV1DecoderStepReset;
        break;
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_audi_v1_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderAudiV1* instance = context;

    audi_v1_extract_fields(&instance->generic, instance->hop, instance->btn_byte);

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    if(ret != SubGhzProtocolStatusOk) {
        FURI_LOG_E(TAG, "Failed to serialize base fields");
        return ret;
    }

    uint8_t hop_data[6];
    for(size_t i = 0; i < 6; i++) {
        hop_data[5 - i] = (instance->hop >> (i * 8)) & 0xFF;
    }
    if(!flipper_format_write_hex(flipper_format, "Hop", hop_data, 6)) {
        FURI_LOG_E(TAG, "Failed to write Hop");
        return SubGhzProtocolStatusError;
    }

    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_audi_v1_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderAudiV1* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_deserialize(&instance->generic, flipper_format);

    if(ret != SubGhzProtocolStatusOk) {
        FURI_LOG_E(TAG, "Failed to deserialize base fields");
        return ret;
    }

    if(instance->generic.data_count_bit != AUDI_V1_DATA_BITS) {
        FURI_LOG_E(TAG, "Invalid bit count: %lu", (unsigned long)instance->generic.data_count_bit);
        return SubGhzProtocolStatusErrorValueBitCount;
    }

    instance->generic.protocol_name = instance->base.protocol->name;

    uint8_t hop_data[6] = {0};
    if(flipper_format_read_hex(flipper_format, "Hop", hop_data, 6)) {
        instance->hop = 0;
        for(size_t i = 0; i < 6; i++) {
            instance->hop = (instance->hop << 8) | hop_data[i];
        }
    } else {

        FURI_LOG_W(TAG, "Hop field missing, reconstructing from Key");
        instance->hop = (instance->generic.data & 0xFFFFFFULL) << 24;
    }

    instance->btn_byte = (instance->generic.data >> 24) & 0xFF;
    audi_v1_extract_fields(&instance->generic, instance->hop, instance->btn_byte);

    uint8_t btn_id = (instance->btn_byte >> 4) & 0x0F;
    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(btn_id);
    }
    subghz_custom_btn_set_max(2);
    subghz_block_generic_global.btn_length_bit = 4;

    return SubGhzProtocolStatusOk;
}

