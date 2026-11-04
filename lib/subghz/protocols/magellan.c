#include "magellan.h"

#include "common.h"
#include "../encode_plugin.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolMagellan"

static const SubGhzBlockConst subghz_protocol_magellan_const = {
    .te_short = 200,
    .te_long = 400,
    .te_delta = 100,
    .min_count_bit_for_found = 32,
};

struct SubGhzProtocolDecoderMagellan {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;
};

struct SubGhzProtocolEncoderMagellan {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderMagellan);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderMagellan);

typedef enum {
    MagellanDecoderStepReset = 0,
    MagellanDecoderStepCheckPreambula,
    MagellanDecoderStepFoundPreambula,
    MagellanDecoderStepSaveDuration,
    MagellanDecoderStepCheckDuration,
} MagellanDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_magellan_decoder = {
    .alloc = subghz_protocol_decoder_magellan_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_magellan_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_magellan_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_magellan_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_magellan");

const SubGhzProtocol subghz_protocol_magellan = {
    .name = SUBGHZ_PROTOCOL_MAGELLAN_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send |
            SubGhzProtocolFlag_Sensors,

    .decoder = &subghz_protocol_magellan_decoder,
    .encoder = &subghz_protocol_magellan_encoder_ext.vtable,
};

void* subghz_protocol_decoder_magellan_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderMagellan), &subghz_protocol_magellan);
}

uint8_t subghz_protocol_magellan_crc8(uint8_t* data, size_t len) {
    uint8_t crc = 0x00;
    size_t i, j;
    for(i = 0; i < len; i++) {
        crc ^= data[i];
        for(j = 0; j < 8; j++) {
            if((crc & 0x80) != 0)
                crc = (uint8_t)((crc << 1) ^ 0x31);
            else
                crc <<= 1;
        }
    }
    return crc;
}

static bool subghz_protocol_magellan_check_crc(SubGhzProtocolDecoderMagellan* instance) {
    uint8_t data[3] = {
        instance->decoder.decode_data >> 24,
        instance->decoder.decode_data >> 16,
        instance->decoder.decode_data >> 8};
    return (instance->decoder.decode_data & 0xFF) ==
           subghz_protocol_magellan_crc8(data, sizeof(data));
}

void subghz_protocol_decoder_magellan_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderMagellan* instance = context;

    switch(instance->decoder.parser_step) {
    case MagellanDecoderStepReset:
        if((level) && (DURATION_DIFF(duration, subghz_protocol_magellan_const.te_short) <
                       subghz_protocol_magellan_const.te_delta)) {
            instance->decoder.parser_step = MagellanDecoderStepCheckPreambula;
            instance->decoder.te_last = duration;
            instance->header_count = 0;
        }
        break;

    case MagellanDecoderStepCheckPreambula:
        if(level) {
            instance->decoder.te_last = duration;
        } else {
            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_magellan_const.te_short) <
                subghz_protocol_magellan_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_magellan_const.te_short) <
                subghz_protocol_magellan_const.te_delta)) {

                instance->header_count++;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_magellan_const.te_short) <
                 subghz_protocol_magellan_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_magellan_const.te_long) <
                 subghz_protocol_magellan_const.te_delta * 2) &&
                (instance->header_count > 10)) {
                instance->decoder.parser_step = MagellanDecoderStepFoundPreambula;
            } else {
                instance->decoder.parser_step = MagellanDecoderStepReset;
            }
        }
        break;

    case MagellanDecoderStepFoundPreambula:
        if(level) {
            instance->decoder.te_last = duration;
        } else {
            if((DURATION_DIFF(
                    instance->decoder.te_last, subghz_protocol_magellan_const.te_short * 6) <
                subghz_protocol_magellan_const.te_delta * 3) &&
               (DURATION_DIFF(duration, subghz_protocol_magellan_const.te_long) <
                subghz_protocol_magellan_const.te_delta * 2)) {
                instance->decoder.parser_step = MagellanDecoderStepSaveDuration;
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
            } else {
                instance->decoder.parser_step = MagellanDecoderStepReset;
            }
        }
        break;

    case MagellanDecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = MagellanDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = MagellanDecoderStepReset;
        }
        break;

    case MagellanDecoderStepCheckDuration:
        if(!level) {
            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_magellan_const.te_short) <
                subghz_protocol_magellan_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_magellan_const.te_long) <
                subghz_protocol_magellan_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = MagellanDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_magellan_const.te_long) <
                 subghz_protocol_magellan_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_magellan_const.te_short) <
                 subghz_protocol_magellan_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = MagellanDecoderStepSaveDuration;
            } else if(duration >= (subghz_protocol_magellan_const.te_long * 3)) {

                if((instance->decoder.decode_count_bit ==
                    subghz_protocol_magellan_const.min_count_bit_for_found) &&
                   subghz_protocol_magellan_check_crc(instance)) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->decoder.parser_step = MagellanDecoderStepReset;
            } else {
                instance->decoder.parser_step = MagellanDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = MagellanDecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_magellan_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderMagellan* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_magellan_const.min_count_bit_for_found);
}

