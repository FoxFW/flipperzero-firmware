#include "nord_ice.h"

#include "common.h"
#include "../encode_plugin.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolNordIce"

#define NORD_ICE_UPLOAD_SIZE 138

static const SubGhzBlockConst subghz_protocol_nord_ice_const = {
    .te_short = 300,
    .te_long = 800,
    .te_delta = 150,
    .min_count_bit_for_found = 33,
};

struct SubGhzProtocolDecoderNordIce {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderNordIce {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderNordIce);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderNordIce);

typedef enum {
    NordIceDecoderStepReset = 0,
    NordIceDecoderStepSaveDuration,
    NordIceDecoderStepCheckDuration,
} NordIceDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_nord_ice_decoder = {
    .alloc = subghz_protocol_decoder_nord_ice_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_nord_ice_feed,
    .reset = subghz_protocol_decoder_nord_ice_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_nord_ice_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_nord_ice_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_nord_ice");

const SubGhzProtocol subghz_protocol_nord_ice = {
    .name = SUBGHZ_PROTOCOL_NORD_ICE_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_nord_ice_decoder,
    .encoder = &subghz_protocol_nord_ice_encoder_ext.vtable,
};

void* subghz_protocol_decoder_nord_ice_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderNordIce), &subghz_protocol_nord_ice);
}

void subghz_protocol_decoder_nord_ice_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderNordIce* instance = context;
    instance->decoder.parser_step = NordIceDecoderStepReset;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_nord_ice_feed(void* context, bool level, volatile uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderNordIce* instance = context;

    switch(instance->decoder.parser_step) {
    case NordIceDecoderStepReset:
        if(!level &&
           DURATION_DIFF(duration, subghz_protocol_nord_ice_const.te_short * 25) <
               subghz_protocol_nord_ice_const.te_delta * 11) {
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = NordIceDecoderStepSaveDuration;
        }
        break;
    case NordIceDecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = NordIceDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = NordIceDecoderStepReset;
        }
        break;
    case NordIceDecoderStepCheckDuration:
        if(!level) {
            if(DURATION_DIFF(
                   instance->decoder.te_last, subghz_protocol_nord_ice_const.te_short) <
                   subghz_protocol_nord_ice_const.te_delta &&
               DURATION_DIFF(duration, subghz_protocol_nord_ice_const.te_long) <
                   subghz_protocol_nord_ice_const.te_delta) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = NordIceDecoderStepSaveDuration;
            } else if(
                DURATION_DIFF(instance->decoder.te_last, subghz_protocol_nord_ice_const.te_long) <
                    subghz_protocol_nord_ice_const.te_delta &&
                DURATION_DIFF(duration, subghz_protocol_nord_ice_const.te_short) <
                    subghz_protocol_nord_ice_const.te_delta) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = NordIceDecoderStepSaveDuration;
            } else if(
                DURATION_DIFF(duration, subghz_protocol_nord_ice_const.te_short * 25) <
                subghz_protocol_nord_ice_const.te_delta * 11) {

                if(DURATION_DIFF(
                       instance->decoder.te_last, subghz_protocol_nord_ice_const.te_short) <
                   subghz_protocol_nord_ice_const.te_delta) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                } else if(
                    DURATION_DIFF(
                        instance->decoder.te_last, subghz_protocol_nord_ice_const.te_long) <
                    subghz_protocol_nord_ice_const.te_delta) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                }
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_nord_ice_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->decoder.parser_step = NordIceDecoderStepReset;
            } else {
                instance->decoder.parser_step = NordIceDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = NordIceDecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_nord_ice_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderNordIce* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_nord_ice_const.min_count_bit_for_found);
}

