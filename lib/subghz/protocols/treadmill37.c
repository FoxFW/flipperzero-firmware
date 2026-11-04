#include "treadmill37.h"

#include "common.h"
#include "../encode_plugin.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolTreadmill37"

static const SubGhzBlockConst subghz_protocol_treadmill37_const = {
    .te_short = 300,
    .te_long = 900,
    .te_delta = 150,
    .min_count_bit_for_found = 37,
};

struct SubGhzProtocolDecoderTreadmill37 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderTreadmill37 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderTreadmill37);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderTreadmill37);

typedef enum {
    Treadmill37DecoderStepReset = 0,
    Treadmill37DecoderStepSaveDuration,
    Treadmill37DecoderStepCheckDuration,
} Treadmill37DecoderStep;

const SubGhzProtocolDecoder subghz_protocol_treadmill37_decoder = {
    .alloc = subghz_protocol_decoder_treadmill37_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_treadmill37_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_treadmill37_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_treadmill37_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_treadmill37");

const SubGhzProtocol subghz_protocol_treadmill37 = {
    .name = SUBGHZ_PROTOCOL_TREADMILL37_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_treadmill37_decoder,
    .encoder = &subghz_protocol_treadmill37_encoder_ext.vtable,
};

void* subghz_protocol_decoder_treadmill37_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderTreadmill37), &subghz_protocol_treadmill37);
}

void subghz_protocol_decoder_treadmill37_feed(
    void* context,
    bool level,
    volatile uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderTreadmill37* instance = context;

    switch(instance->decoder.parser_step) {
    case Treadmill37DecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_treadmill37_const.te_short * 20) <
                        subghz_protocol_treadmill37_const.te_delta * 4)) {

            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = Treadmill37DecoderStepSaveDuration;
        }
        break;
    case Treadmill37DecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = Treadmill37DecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = Treadmill37DecoderStepReset;
        }
        break;
    case Treadmill37DecoderStepCheckDuration:
        if(!level) {

            if((DURATION_DIFF(
                    instance->decoder.te_last, subghz_protocol_treadmill37_const.te_short) <
                subghz_protocol_treadmill37_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_treadmill37_const.te_long) <
                subghz_protocol_treadmill37_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = Treadmill37DecoderStepSaveDuration;

            } else if(
                (DURATION_DIFF(
                     instance->decoder.te_last, subghz_protocol_treadmill37_const.te_long) <
                 subghz_protocol_treadmill37_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_treadmill37_const.te_short) <
                 subghz_protocol_treadmill37_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = Treadmill37DecoderStepSaveDuration;
            } else if(

                DURATION_DIFF(duration, subghz_protocol_treadmill37_const.te_short * 20) <
                subghz_protocol_treadmill37_const.te_delta * 4) {

                if((DURATION_DIFF(
                        instance->decoder.te_last, subghz_protocol_treadmill37_const.te_short) <
                    subghz_protocol_treadmill37_const.te_delta)) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                }
                if((DURATION_DIFF(
                        instance->decoder.te_last, subghz_protocol_treadmill37_const.te_long) <
                    subghz_protocol_treadmill37_const.te_delta)) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                }

                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_treadmill37_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->decoder.parser_step = Treadmill37DecoderStepReset;
            } else {
                instance->decoder.parser_step = Treadmill37DecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = Treadmill37DecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_treadmill37_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderTreadmill37* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_treadmill37_const.min_count_bit_for_found);
}

