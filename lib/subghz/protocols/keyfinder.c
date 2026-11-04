#include "keyfinder.h"

#include "common.h"
#include "../encode_plugin.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolKeyFinder"

static const SubGhzBlockConst subghz_protocol_keyfinder_const = {
    .te_short = 400,
    .te_long = 1200,
    .te_delta = 150,
    .min_count_bit_for_found = 24,
};

struct SubGhzProtocolDecoderKeyFinder {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint8_t end_count;
};

struct SubGhzProtocolEncoderKeyFinder {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderKeyFinder);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderKeyFinder);

typedef enum {
    KeyFinderDecoderStepReset = 0,
    KeyFinderDecoderStepSaveDuration,
    KeyFinderDecoderStepCheckDuration,
    KeyFinderDecoderStepFinish,
} KeyFinderDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_keyfinder_decoder = {
    .alloc = subghz_protocol_decoder_keyfinder_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_keyfinder_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_keyfinder_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_keyfinder_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_keyfinder");

const SubGhzProtocol subghz_protocol_keyfinder = {
    .name = SUBGHZ_PROTOCOL_KEYFINDER_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_keyfinder_decoder,
    .encoder = &subghz_protocol_keyfinder_encoder_ext.vtable,
};

void* subghz_protocol_decoder_keyfinder_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderKeyFinder), &subghz_protocol_keyfinder);
}

void subghz_protocol_decoder_keyfinder_feed(void* context, bool level, volatile uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderKeyFinder* instance = context;

    switch(instance->decoder.parser_step) {
    case KeyFinderDecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_keyfinder_const.te_short * 10) <
                        subghz_protocol_keyfinder_const.te_delta * 5)) {

            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = KeyFinderDecoderStepSaveDuration;
        }
        break;
    case KeyFinderDecoderStepSaveDuration:
        if(instance->decoder.decode_count_bit ==
           subghz_protocol_keyfinder_const.min_count_bit_for_found) {
            if((level) && (DURATION_DIFF(duration, subghz_protocol_keyfinder_const.te_short) <
                           subghz_protocol_keyfinder_const.te_delta)) {
                instance->end_count++;
                if(instance->end_count == 4) {
                    instance->decoder.parser_step = KeyFinderDecoderStepFinish;
                    instance->end_count = 0;
                }
            } else if(
                (!level) && (DURATION_DIFF(duration, subghz_protocol_keyfinder_const.te_short) <
                             subghz_protocol_keyfinder_const.te_delta)) {
                break;
            } else {
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->end_count = 0;
                instance->decoder.parser_step = KeyFinderDecoderStepReset;
            }
            break;
        }

        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = KeyFinderDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = KeyFinderDecoderStepReset;
        }
        break;
    case KeyFinderDecoderStepCheckDuration:
        if(!level) {

            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_keyfinder_const.te_short) <
                subghz_protocol_keyfinder_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_keyfinder_const.te_long) <
                subghz_protocol_keyfinder_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = KeyFinderDecoderStepSaveDuration;

            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_keyfinder_const.te_long) <
                 subghz_protocol_keyfinder_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_keyfinder_const.te_short) <
                 subghz_protocol_keyfinder_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = KeyFinderDecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = KeyFinderDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = KeyFinderDecoderStepReset;
        }
        break;
    case KeyFinderDecoderStepFinish:

        if(instance->decoder.decode_count_bit ==
           subghz_protocol_keyfinder_const.min_count_bit_for_found) {
            instance->generic.data = instance->decoder.decode_data;
            instance->generic.data_count_bit = instance->decoder.decode_count_bit;
            if(instance->base.callback)
                instance->base.callback(&instance->base, instance->base.context);
        }
        instance->decoder.decode_data = 0;
        instance->decoder.decode_count_bit = 0;
        instance->end_count = 0;
        instance->decoder.parser_step = KeyFinderDecoderStepReset;
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_keyfinder_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderKeyFinder* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_keyfinder_const.min_count_bit_for_found);
}

