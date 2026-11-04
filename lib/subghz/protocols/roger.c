#include "roger.h"

#include "common.h"
#include "../encode_plugin.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#include "../blocks/custom_btn_i.h"

#define TAG "SubGhzProtocolRoger"

static const SubGhzBlockConst subghz_protocol_roger_const = {
    .te_short = 500,
    .te_long = 1000,
    .te_delta = 270,
    .min_count_bit_for_found = 28,
};

struct SubGhzProtocolDecoderRoger {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderRoger {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderRoger);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderRoger);

typedef enum {
    RogerDecoderStepReset = 0,
    RogerDecoderStepSaveDuration,
    RogerDecoderStepCheckDuration,
} RogerDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_roger_decoder = {
    .alloc = subghz_protocol_decoder_roger_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_roger_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_roger_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_roger_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_roger");

const SubGhzProtocol subghz_protocol_roger = {
    .name = SUBGHZ_PROTOCOL_ROGER_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_roger_decoder,
    .encoder = &subghz_protocol_roger_encoder_ext.vtable,
};

void* subghz_protocol_decoder_roger_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderRoger), &subghz_protocol_roger);
}

void subghz_protocol_decoder_roger_feed(void* context, bool level, volatile uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderRoger* instance = context;

    switch(instance->decoder.parser_step) {
    case RogerDecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_roger_const.te_short * 19) <
                        subghz_protocol_roger_const.te_delta * 5)) {

            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = RogerDecoderStepSaveDuration;
        }
        break;
    case RogerDecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = RogerDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = RogerDecoderStepReset;
        }
        break;
    case RogerDecoderStepCheckDuration:
        if(!level) {

            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_roger_const.te_long) <
                subghz_protocol_roger_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_roger_const.te_short) <
                subghz_protocol_roger_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = RogerDecoderStepSaveDuration;

            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_roger_const.te_short) <
                 subghz_protocol_roger_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_roger_const.te_long) <
                 subghz_protocol_roger_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = RogerDecoderStepSaveDuration;
            } else if(

                DURATION_DIFF(duration, subghz_protocol_roger_const.te_short * 19) <
                subghz_protocol_roger_const.te_delta * 5) {

                if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_roger_const.te_long) <
                    subghz_protocol_roger_const.te_delta)) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                }
                if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_roger_const.te_short) <
                    subghz_protocol_roger_const.te_delta)) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                }

                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_roger_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->decoder.parser_step = RogerDecoderStepReset;
            } else {
                instance->decoder.parser_step = RogerDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = RogerDecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_roger_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderRoger* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_roger_const.min_count_bit_for_found);
}

