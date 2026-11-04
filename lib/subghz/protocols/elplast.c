#include "elplast.h"

#include "common.h"
#include "../encode_plugin.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolElplast"

static const SubGhzBlockConst subghz_protocol_elplast_const = {
    .te_short = 230,
    .te_long = 1550,
    .te_delta = 160,
    .min_count_bit_for_found = 18,
};

struct SubGhzProtocolDecoderElplast {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderElplast {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderElplast);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderElplast);

typedef enum {
    ElplastDecoderStepReset = 0,
    ElplastDecoderStepSaveDuration,
    ElplastDecoderStepCheckDuration,
} ElplastDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_elplast_decoder = {
    .alloc = subghz_protocol_decoder_elplast_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_elplast_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_elplast_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_elplast_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_elplast");

const SubGhzProtocol subghz_protocol_elplast = {
    .name = SUBGHZ_PROTOCOL_ELPLAST_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_elplast_decoder,
    .encoder = &subghz_protocol_elplast_encoder_ext.vtable,
};

void* subghz_protocol_decoder_elplast_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderElplast), &subghz_protocol_elplast);
}

void subghz_protocol_decoder_elplast_feed(void* context, bool level, volatile uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderElplast* instance = context;

    switch(instance->decoder.parser_step) {
    case ElplastDecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_elplast_const.te_long * 8) <
                        subghz_protocol_elplast_const.te_delta * 13)) {

            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = ElplastDecoderStepSaveDuration;
        }
        break;
    case ElplastDecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = ElplastDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = ElplastDecoderStepReset;
        }
        break;
    case ElplastDecoderStepCheckDuration:
        if(!level) {

            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_elplast_const.te_long) <
                subghz_protocol_elplast_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_elplast_const.te_short) <
                subghz_protocol_elplast_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = ElplastDecoderStepSaveDuration;

            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_elplast_const.te_short) <
                 subghz_protocol_elplast_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_elplast_const.te_long) <
                 subghz_protocol_elplast_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = ElplastDecoderStepSaveDuration;
            } else if(

                DURATION_DIFF(duration, subghz_protocol_elplast_const.te_long * 8) <
                subghz_protocol_elplast_const.te_delta * 13) {

                if((DURATION_DIFF(
                        instance->decoder.te_last, subghz_protocol_elplast_const.te_long) <
                    subghz_protocol_elplast_const.te_delta)) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                }
                if((DURATION_DIFF(
                        instance->decoder.te_last, subghz_protocol_elplast_const.te_short) <
                    subghz_protocol_elplast_const.te_delta)) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                }

                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_elplast_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->decoder.parser_step = ElplastDecoderStepReset;
            } else {
                instance->decoder.parser_step = ElplastDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = ElplastDecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_elplast_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderElplast* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_elplast_const.min_count_bit_for_found);
}

