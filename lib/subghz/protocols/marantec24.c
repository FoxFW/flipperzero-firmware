#include "marantec24.h"

#include "common.h"
#include "../encode_plugin.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolMarantec24"

static const SubGhzBlockConst subghz_protocol_marantec24_const = {
    .te_short = 800,
    .te_long = 1600,
    .te_delta = 200,
    .min_count_bit_for_found = 24,
};

struct SubGhzProtocolDecoderMarantec24 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderMarantec24 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderMarantec24);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderMarantec24);

typedef enum {
    Marantec24DecoderStepReset = 0,
    Marantec24DecoderStepSaveDuration,
    Marantec24DecoderStepCheckDuration,
} Marantec24DecoderStep;

const SubGhzProtocolDecoder subghz_protocol_marantec24_decoder = {
    .alloc = subghz_protocol_decoder_marantec24_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_marantec24_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_marantec24_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_marantec24_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_marantec24");

const SubGhzProtocol subghz_protocol_marantec24 = {
    .name = SUBGHZ_PROTOCOL_MARANTEC24_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_868 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_marantec24_decoder,
    .encoder = &subghz_protocol_marantec24_encoder_ext.vtable,
};

void* subghz_protocol_decoder_marantec24_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderMarantec24), &subghz_protocol_marantec24);
}

void subghz_protocol_decoder_marantec24_feed(void* context, bool level, volatile uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderMarantec24* instance = context;

    switch(instance->decoder.parser_step) {
    case Marantec24DecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_marantec24_const.te_long * 9) <
                        subghz_protocol_marantec24_const.te_delta * 4)) {

            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = Marantec24DecoderStepSaveDuration;
        }
        break;
    case Marantec24DecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = Marantec24DecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = Marantec24DecoderStepReset;
        }
        break;
    case Marantec24DecoderStepCheckDuration:
        if(!level) {

            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_marantec24_const.te_long) <
                subghz_protocol_marantec24_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_marantec24_const.te_short * 3) <
                subghz_protocol_marantec24_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = Marantec24DecoderStepSaveDuration;

            } else if(
                (DURATION_DIFF(
                     instance->decoder.te_last, subghz_protocol_marantec24_const.te_short) <
                 subghz_protocol_marantec24_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_marantec24_const.te_long * 2) <
                 subghz_protocol_marantec24_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = Marantec24DecoderStepSaveDuration;
            } else if(

                DURATION_DIFF(duration, subghz_protocol_marantec24_const.te_long * 9) <
                subghz_protocol_marantec24_const.te_delta * 4) {

                if((DURATION_DIFF(
                        instance->decoder.te_last, subghz_protocol_marantec24_const.te_long) <
                    subghz_protocol_marantec24_const.te_delta)) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                }
                if((DURATION_DIFF(
                        instance->decoder.te_last, subghz_protocol_marantec24_const.te_short) <
                    subghz_protocol_marantec24_const.te_delta)) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                }

                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_marantec24_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->decoder.parser_step = Marantec24DecoderStepReset;
            } else {
                instance->decoder.parser_step = Marantec24DecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = Marantec24DecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_marantec24_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderMarantec24* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_marantec24_const.min_count_bit_for_found);
}

