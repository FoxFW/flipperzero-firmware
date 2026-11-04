#include "hormann.h"

#include "common.h"
#include "../encode_plugin.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolHormannHsm"

#define HORMANN_HSM_PATTERN 0xFF000000003

static const SubGhzBlockConst subghz_protocol_hormann_const = {
    .te_short = 500,
    .te_long = 1000,
    .te_delta = 200,
    .min_count_bit_for_found = 44,
};

struct SubGhzProtocolDecoderHormann {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderHormann {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderHormann);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderHormann);

typedef enum {
    HormannDecoderStepReset = 0,
    HormannDecoderStepFoundStartHeader,
    HormannDecoderStepFoundHeader,
    HormannDecoderStepFoundStartBit,
    HormannDecoderStepSaveDuration,
    HormannDecoderStepCheckDuration,
} HormannDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_hormann_decoder = {
    .alloc = subghz_protocol_decoder_hormann_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_hormann_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_hormann_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_hormann_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_hormann");

const SubGhzProtocol subghz_protocol_hormann = {
    .name = SUBGHZ_PROTOCOL_HORMANN_HSM_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_hormann_decoder,
    .encoder = &subghz_protocol_hormann_encoder_ext.vtable,
};

void* subghz_protocol_decoder_hormann_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderHormann), &subghz_protocol_hormann);
}

static bool subghz_protocol_decoder_hormann_check_pattern(SubGhzProtocolDecoderHormann* instance) {
    return (instance->decoder.decode_data & HORMANN_HSM_PATTERN) == HORMANN_HSM_PATTERN;
}

void subghz_protocol_decoder_hormann_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderHormann* instance = context;

    switch(instance->decoder.parser_step) {
    case HormannDecoderStepReset:
        if((level) && (DURATION_DIFF(duration, subghz_protocol_hormann_const.te_short * 24) <
                       subghz_protocol_hormann_const.te_delta * 24)) {
            instance->decoder.parser_step = HormannDecoderStepFoundStartBit;
        }
        break;
    case HormannDecoderStepFoundStartBit:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_hormann_const.te_short) <
                        subghz_protocol_hormann_const.te_delta)) {
            instance->decoder.parser_step = HormannDecoderStepSaveDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
        } else {
            instance->decoder.parser_step = HormannDecoderStepReset;
        }
        break;
    case HormannDecoderStepSaveDuration:
        if(level) {
            if(duration >= (subghz_protocol_hormann_const.te_short * 5) &&
               subghz_protocol_decoder_hormann_check_pattern(instance)) {
                instance->decoder.parser_step = HormannDecoderStepFoundStartBit;
                if(instance->decoder.decode_count_bit >=
                   subghz_protocol_hormann_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                break;
            }
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = HormannDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = HormannDecoderStepReset;
        }
        break;
    case HormannDecoderStepCheckDuration:
        if(!level) {
            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_hormann_const.te_short) <
                subghz_protocol_hormann_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_hormann_const.te_long) <
                subghz_protocol_hormann_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = HormannDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_hormann_const.te_long) <
                 subghz_protocol_hormann_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_hormann_const.te_short) <
                 subghz_protocol_hormann_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = HormannDecoderStepSaveDuration;
            } else
                instance->decoder.parser_step = HormannDecoderStepReset;
        } else {
            instance->decoder.parser_step = HormannDecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_hormann_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderHormann* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_hormann_const.min_count_bit_for_found);
}

