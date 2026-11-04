#include "marantec.h"

#include "common.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolMarantec"

static const SubGhzBlockConst subghz_protocol_marantec_const = {
    .te_short = 1000,
    .te_long = 2000,
    .te_delta = 200,
    .min_count_bit_for_found = 49,
};

struct SubGhzProtocolDecoderMarantec {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    ManchesterState manchester_saved_state;
    uint16_t header_count;
};

struct SubGhzProtocolEncoderMarantec {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderMarantec);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderMarantec);

typedef enum {
    MarantecDecoderStepReset = 0,
    MarantecDecoderFoundHeader,
    MarantecDecoderStepDecoderData,
} MarantecDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_marantec_decoder = {
    .alloc = subghz_protocol_decoder_marantec_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_marantec_feed,
    .reset = subghz_protocol_decoder_marantec_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_marantec_deserialize,
    .get_string = subghz_protocol_decoder_marantec_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_marantec_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_marantec");

const SubGhzProtocol subghz_protocol_marantec = {
    .name = SUBGHZ_PROTOCOL_MARANTEC_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_marantec_decoder,
    .encoder = &subghz_protocol_marantec_encoder_ext.vtable,
};

uint8_t subghz_protocol_marantec_crc8(uint8_t* data, size_t len) {
    uint8_t crc = 0x01;
    size_t i, j;
    for(i = 0; i < len; i++) {
        crc ^= data[i];
        for(j = 0; j < 8; j++) {
            if((crc & 0x80) != 0)
                crc = (uint8_t)((crc << 1) ^ 0x1D);
            else
                crc <<= 1;
        }
    }
    return crc;
}

void* subghz_protocol_decoder_marantec_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderMarantec), &subghz_protocol_marantec);
}

void subghz_protocol_decoder_marantec_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderMarantec* instance = context;
    instance->decoder.parser_step = MarantecDecoderStepReset;
    manchester_advance(
        instance->manchester_saved_state,
        ManchesterEventReset,
        &instance->manchester_saved_state,
        NULL);
}

void subghz_protocol_decoder_marantec_feed(void* context, bool level, volatile uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderMarantec* instance = context;
    ManchesterEvent event = ManchesterEventReset;

    switch(instance->decoder.parser_step) {
    case MarantecDecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_marantec_const.te_long * 5) <
                        subghz_protocol_marantec_const.te_delta * 8)) {

            instance->decoder.parser_step = MarantecDecoderStepDecoderData;
            instance->decoder.decode_data = 1;
            instance->decoder.decode_count_bit = 1;
            manchester_advance(
                instance->manchester_saved_state,
                ManchesterEventReset,
                &instance->manchester_saved_state,
                NULL);
        }
        break;
    case MarantecDecoderStepDecoderData:
        if(!level) {
            if(DURATION_DIFF(duration, subghz_protocol_marantec_const.te_short) <
               subghz_protocol_marantec_const.te_delta) {
                event = ManchesterEventShortLow;
            } else if(
                DURATION_DIFF(duration, subghz_protocol_marantec_const.te_long) <
                subghz_protocol_marantec_const.te_delta) {
                event = ManchesterEventLongLow;
            } else if(
                duration >= ((uint32_t)subghz_protocol_marantec_const.te_long * 2 +
                             subghz_protocol_marantec_const.te_delta)) {
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_marantec_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.decode_data = 1;
                instance->decoder.decode_count_bit = 1;
                manchester_advance(
                    instance->manchester_saved_state,
                    ManchesterEventReset,
                    &instance->manchester_saved_state,
                    NULL);
            } else {
                instance->decoder.parser_step = MarantecDecoderStepReset;
            }
        } else {
            if(DURATION_DIFF(duration, subghz_protocol_marantec_const.te_short) <
               subghz_protocol_marantec_const.te_delta) {
                event = ManchesterEventShortHigh;
            } else if(
                DURATION_DIFF(duration, subghz_protocol_marantec_const.te_long) <
                subghz_protocol_marantec_const.te_delta) {
                event = ManchesterEventLongHigh;
            } else {
                instance->decoder.parser_step = MarantecDecoderStepReset;
            }
        }
        if(event != ManchesterEventReset) {
            bool data;
            bool data_ok = manchester_advance(
                instance->manchester_saved_state, event, &instance->manchester_saved_state, &data);

            if(data_ok) {
                instance->decoder.decode_data = (instance->decoder.decode_data << 1) | data;
                instance->decoder.decode_count_bit++;
            }
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_marantec_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderMarantec* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_marantec_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_marantec_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderMarantec* instance = context;
    // Detect/decode split (FoxFW2.0 v2.1): the CRC helper is shared with the
    // encoder and not exported, so the CRC is computed here, exactly as before.
    uint8_t tdata[6] = {
        instance->generic.data >> 48,
        instance->generic.data >> 40,
        instance->generic.data >> 32,
        instance->generic.data >> 24,
        instance->generic.data >> 16,
        instance->generic.data >> 8};
    const uint64_t extra[1] = {subghz_protocol_marantec_crc8(tdata, sizeof(tdata))};
    const SubGhzDecodeInput input = {
        .decoder = context,
        .generic = &instance->generic,
        .extra = extra,
        .extra_count = 1,
        .aux = NULL,
    };
    subghz_decode_plugin_get_string(instance->generic.protocol_name, &input, output);
}

