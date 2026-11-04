#include "hollarm.h"

#include "common.h"
#include "../encode_plugin.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#include "../blocks/custom_btn_i.h"

#define TAG "SubGhzProtocolHollarm"

static const SubGhzBlockConst subghz_protocol_hollarm_const = {
    .te_short = 200,
    .te_long = 1000,
    .te_delta = 200,
    .min_count_bit_for_found = 42,
};

struct SubGhzProtocolDecoderHollarm {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderHollarm {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderHollarm);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderHollarm);

typedef enum {
    HollarmDecoderStepReset = 0,
    HollarmDecoderStepSaveDuration,
    HollarmDecoderStepCheckDuration,
} HollarmDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_hollarm_decoder = {
    .alloc = subghz_protocol_decoder_hollarm_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_hollarm_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_hollarm_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_hollarm_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_hollarm");

const SubGhzProtocol subghz_protocol_hollarm = {
    .name = SUBGHZ_PROTOCOL_HOLLARM_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send |
            SubGhzProtocolFlag_Alarms,

    .decoder = &subghz_protocol_hollarm_decoder,
    .encoder = &subghz_protocol_hollarm_encoder_ext.vtable,
};

void* subghz_protocol_decoder_hollarm_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderHollarm), &subghz_protocol_hollarm);
}

void subghz_protocol_decoder_hollarm_feed(void* context, bool level, volatile uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderHollarm* instance = context;

    switch(instance->decoder.parser_step) {
    case HollarmDecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_hollarm_const.te_short * 12) <
                        subghz_protocol_hollarm_const.te_delta * 2)) {

            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = HollarmDecoderStepSaveDuration;
        }
        break;
    case HollarmDecoderStepSaveDuration:

        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = HollarmDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = HollarmDecoderStepReset;
        }
        break;
    case HollarmDecoderStepCheckDuration:
        if(!level) {

            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_hollarm_const.te_short) <
                subghz_protocol_hollarm_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_hollarm_const.te_long) <
                subghz_protocol_hollarm_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = HollarmDecoderStepSaveDuration;

            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_hollarm_const.te_short) <
                 subghz_protocol_hollarm_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_hollarm_const.te_short * 8) <
                 subghz_protocol_hollarm_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = HollarmDecoderStepSaveDuration;
            } else if(

                DURATION_DIFF(duration, subghz_protocol_hollarm_const.te_short * 12) <
                subghz_protocol_hollarm_const.te_delta) {

                subghz_protocol_blocks_add_bit(&instance->decoder, 0);

                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_hollarm_const.min_count_bit_for_found) {

                    instance->generic.data = (instance->decoder.decode_data >> 2);
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                    uint8_t bytesum = ((instance->generic.data >> 32) & 0xFF) +
                                      ((instance->generic.data >> 24) & 0xFF) +
                                      ((instance->generic.data >> 16) & 0xFF) +
                                      ((instance->generic.data >> 8) & 0xFF);

                    if(bytesum != (instance->generic.data & 0xFF)) {

                        instance->generic.data = 0;
                        instance->generic.data_count_bit = 0;
                        instance->decoder.decode_data = 0;
                        instance->decoder.decode_count_bit = 0;
                        instance->decoder.parser_step = HollarmDecoderStepReset;
                        break;
                    }
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->decoder.parser_step = HollarmDecoderStepReset;
            } else {
                instance->decoder.parser_step = HollarmDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = HollarmDecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_hollarm_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderHollarm* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_hollarm_const.min_count_bit_for_found);
}

