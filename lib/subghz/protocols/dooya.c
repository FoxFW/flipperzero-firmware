#include "dooya.h"

#include "common.h"
#include "../encode_plugin.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolDooya"

#define DOYA_SINGLE_CHANNEL 0xFF

static const SubGhzBlockConst subghz_protocol_dooya_const = {
    .te_short = 366,
    .te_long = 733,
    .te_delta = 120,
    .min_count_bit_for_found = 40,
};

struct SubGhzProtocolDecoderDooya {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderDooya {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderDooya);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderDooya);

typedef enum {
    DooyaDecoderStepReset = 0,
    DooyaDecoderStepFoundStartBit,
    DooyaDecoderStepSaveDuration,
    DooyaDecoderStepCheckDuration,
} DooyaDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_dooya_decoder = {
    .alloc = subghz_protocol_decoder_dooya_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_dooya_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_dooya_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_dooya_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_dooya");

const SubGhzProtocol subghz_protocol_dooya = {
    .name = SUBGHZ_PROTOCOL_DOOYA_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_dooya_decoder,
    .encoder = &subghz_protocol_dooya_encoder_ext.vtable,
};

void* subghz_protocol_decoder_dooya_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderDooya), &subghz_protocol_dooya);
}

void subghz_protocol_decoder_dooya_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderDooya* instance = context;

    switch(instance->decoder.parser_step) {
    case DooyaDecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_dooya_const.te_long * 12) <
                        subghz_protocol_dooya_const.te_delta * 20)) {
            instance->decoder.parser_step = DooyaDecoderStepFoundStartBit;
        }
        break;

    case DooyaDecoderStepFoundStartBit:
        if(!level) {
            if(DURATION_DIFF(duration, subghz_protocol_dooya_const.te_long * 2) <
               subghz_protocol_dooya_const.te_delta * 3) {
                instance->decoder.parser_step = DooyaDecoderStepSaveDuration;
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
            } else {
                instance->decoder.parser_step = DooyaDecoderStepReset;
            }
        } else if(
            DURATION_DIFF(duration, subghz_protocol_dooya_const.te_short * 13) <
            subghz_protocol_dooya_const.te_delta * 5) {
            break;
        } else {
            instance->decoder.parser_step = DooyaDecoderStepReset;
        }
        break;

    case DooyaDecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = DooyaDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = DooyaDecoderStepReset;
        }
        break;

    case DooyaDecoderStepCheckDuration:
        if(!level) {
            if(duration >= (subghz_protocol_dooya_const.te_long * 4)) {

                if(DURATION_DIFF(instance->decoder.te_last, subghz_protocol_dooya_const.te_short) <
                   subghz_protocol_dooya_const.te_delta) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                } else if(
                    DURATION_DIFF(instance->decoder.te_last, subghz_protocol_dooya_const.te_long) <
                    subghz_protocol_dooya_const.te_delta * 2) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                } else {
                    instance->decoder.parser_step = DooyaDecoderStepReset;
                    break;
                }
                instance->decoder.parser_step = DooyaDecoderStepFoundStartBit;
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_dooya_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                break;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_dooya_const.te_short) <
                 subghz_protocol_dooya_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_dooya_const.te_long) <
                 subghz_protocol_dooya_const.te_delta * 2)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = DooyaDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_dooya_const.te_long) <
                 subghz_protocol_dooya_const.te_delta * 2) &&
                (DURATION_DIFF(duration, subghz_protocol_dooya_const.te_short) <
                 subghz_protocol_dooya_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = DooyaDecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = DooyaDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = DooyaDecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_dooya_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderDooya* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_dooya_const.min_count_bit_for_found);
}

