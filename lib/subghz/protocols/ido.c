#include "ido.h"

#include "common.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolIdo117/111"

static const SubGhzBlockConst subghz_protocol_ido_const = {
    .te_short = 450,
    .te_long = 1450,
    .te_delta = 150,
    .min_count_bit_for_found = 48,
};

struct SubGhzProtocolDecoderIDo {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderIDo {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderIDo);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderIDo);

typedef enum {
    IDoDecoderStepReset = 0,
    IDoDecoderStepFoundPreambula,
    IDoDecoderStepSaveDuration,
    IDoDecoderStepCheckDuration,
} IDoDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_ido_decoder = {
    .alloc = subghz_protocol_decoder_ido_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_ido_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .deserialize = subghz_protocol_decoder_ido_deserialize,
    .serialize = subghz_protocol_decoder_common_serialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_ido_encoder = {
    .alloc = NULL,
    .free = NULL,

    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

const SubGhzProtocol subghz_protocol_ido = {
    .name = SUBGHZ_PROTOCOL_IDO_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Save,

    .decoder = &subghz_protocol_ido_decoder,
    .encoder = &subghz_protocol_ido_encoder,
};

void* subghz_protocol_decoder_ido_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderIDo), &subghz_protocol_ido);
}

void subghz_protocol_decoder_ido_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderIDo* instance = context;

    switch(instance->decoder.parser_step) {
    case IDoDecoderStepReset:
        if((level) && (DURATION_DIFF(duration, subghz_protocol_ido_const.te_short * 10) <
                       subghz_protocol_ido_const.te_delta * 5)) {
            instance->decoder.parser_step = IDoDecoderStepFoundPreambula;
        }
        break;
    case IDoDecoderStepFoundPreambula:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_ido_const.te_short * 10) <
                        subghz_protocol_ido_const.te_delta * 5)) {

            instance->decoder.parser_step = IDoDecoderStepSaveDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
        } else {
            instance->decoder.parser_step = IDoDecoderStepReset;
        }
        break;
    case IDoDecoderStepSaveDuration:
        if(level) {
            if(duration >= ((uint32_t)subghz_protocol_ido_const.te_short * 5 +
                            subghz_protocol_ido_const.te_delta)) {
                instance->decoder.parser_step = IDoDecoderStepFoundPreambula;
                if(instance->decoder.decode_count_bit >=
                   subghz_protocol_ido_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                break;
            } else {
                instance->decoder.te_last = duration;
                instance->decoder.parser_step = IDoDecoderStepCheckDuration;
            }

        } else {
            instance->decoder.parser_step = IDoDecoderStepReset;
        }
        break;
    case IDoDecoderStepCheckDuration:
        if(!level) {
            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_ido_const.te_short) <
                subghz_protocol_ido_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_ido_const.te_long) <
                subghz_protocol_ido_const.te_delta * 3)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = IDoDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_ido_const.te_short) <
                 subghz_protocol_ido_const.te_delta * 3) &&
                (DURATION_DIFF(duration, subghz_protocol_ido_const.te_short) <
                 subghz_protocol_ido_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = IDoDecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = IDoDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = IDoDecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_ido_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderIDo* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_ido_const.min_count_bit_for_found);
}

