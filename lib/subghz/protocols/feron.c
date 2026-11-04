#include "feron.h"

#include "common.h"
#include "../encode_plugin.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolFeron"

static const SubGhzBlockConst subghz_protocol_feron_const = {
    .te_short = 350,
    .te_long = 750,
    .te_delta = 150,
    .min_count_bit_for_found = 32,
};

struct SubGhzProtocolDecoderFeron {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderFeron {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderFeron);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderFeron);

typedef enum {
    FeronDecoderStepReset = 0,
    FeronDecoderStepSaveDuration,
    FeronDecoderStepCheckDuration,
} FeronDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_feron_decoder = {
    .alloc = subghz_protocol_decoder_feron_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_feron_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_feron_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_feron_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_feron");

const SubGhzProtocol subghz_protocol_feron = {
    .name = SUBGHZ_PROTOCOL_FERON_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send |
            SubGhzProtocolFlag_Sensors,

    .decoder = &subghz_protocol_feron_decoder,
    .encoder = &subghz_protocol_feron_encoder_ext.vtable,
};

void* subghz_protocol_decoder_feron_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderFeron), &subghz_protocol_feron);
}

void subghz_protocol_decoder_feron_feed(void* context, bool level, volatile uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderFeron* instance = context;

    switch(instance->decoder.parser_step) {
    case FeronDecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_feron_const.te_long * 6) <
                        subghz_protocol_feron_const.te_delta * 4)) {

            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = FeronDecoderStepSaveDuration;
        }
        break;
    case FeronDecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = FeronDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = FeronDecoderStepReset;
        }
        break;
    case FeronDecoderStepCheckDuration:
        if(!level) {

            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_feron_const.te_short) <
                subghz_protocol_feron_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_feron_const.te_long) <
                subghz_protocol_feron_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = FeronDecoderStepSaveDuration;

            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_feron_const.te_long) <
                 subghz_protocol_feron_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_feron_const.te_short) <
                 subghz_protocol_feron_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = FeronDecoderStepSaveDuration;
            } else if(

                DURATION_DIFF(
                    duration, (uint16_t)(subghz_protocol_feron_const.te_short + (uint16_t)150)) <
                subghz_protocol_feron_const.te_delta) {
                if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_feron_const.te_short) <
                    subghz_protocol_feron_const.te_delta)) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                }
                if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_feron_const.te_long) <
                    subghz_protocol_feron_const.te_delta)) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                }

                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_feron_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->decoder.parser_step = FeronDecoderStepReset;
            } else {
                instance->decoder.parser_step = FeronDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = FeronDecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_feron_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderFeron* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_feron_const.min_count_bit_for_found);
}

