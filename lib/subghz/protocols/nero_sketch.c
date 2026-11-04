#include "nero_sketch.h"

#include "common.h"
#include "../encode_plugin.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolNeroSketch"

static const SubGhzBlockConst subghz_protocol_nero_sketch_const = {
    .te_short = 330,
    .te_long = 660,
    .te_delta = 150,
    .min_count_bit_for_found = 40,
};

struct SubGhzProtocolDecoderNeroSketch {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;
};

struct SubGhzProtocolEncoderNeroSketch {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderNeroSketch);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderNeroSketch);

typedef enum {
    NeroSketchDecoderStepReset = 0,
    NeroSketchDecoderStepCheckPreambula,
    NeroSketchDecoderStepSaveDuration,
    NeroSketchDecoderStepCheckDuration,
} NeroSketchDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_nero_sketch_decoder = {
    .alloc = subghz_protocol_decoder_nero_sketch_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_nero_sketch_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_nero_sketch_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_nero_sketch_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_nero_sketch");

const SubGhzProtocol subghz_protocol_nero_sketch = {
    .name = SUBGHZ_PROTOCOL_NERO_SKETCH_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_nero_sketch_decoder,
    .encoder = &subghz_protocol_nero_sketch_encoder_ext.vtable,
};

void* subghz_protocol_decoder_nero_sketch_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderNeroSketch), &subghz_protocol_nero_sketch);
}

void subghz_protocol_decoder_nero_sketch_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderNeroSketch* instance = context;

    switch(instance->decoder.parser_step) {
    case NeroSketchDecoderStepReset:
        if((level) && (DURATION_DIFF(duration, subghz_protocol_nero_sketch_const.te_short) <
                       subghz_protocol_nero_sketch_const.te_delta)) {
            instance->decoder.parser_step = NeroSketchDecoderStepCheckPreambula;
            instance->decoder.te_last = duration;
            instance->header_count = 0;
        }
        break;
    case NeroSketchDecoderStepCheckPreambula:
        if(level) {
            if((DURATION_DIFF(duration, subghz_protocol_nero_sketch_const.te_short) <
                subghz_protocol_nero_sketch_const.te_delta) ||
               (DURATION_DIFF(duration, subghz_protocol_nero_sketch_const.te_short * 4) <
                subghz_protocol_nero_sketch_const.te_delta)) {
                instance->decoder.te_last = duration;
            } else {
                instance->decoder.parser_step = NeroSketchDecoderStepReset;
            }
        } else if(
            DURATION_DIFF(duration, subghz_protocol_nero_sketch_const.te_short) <
            subghz_protocol_nero_sketch_const.te_delta) {
            if(DURATION_DIFF(
                   instance->decoder.te_last, subghz_protocol_nero_sketch_const.te_short) <
               subghz_protocol_nero_sketch_const.te_delta) {

                instance->header_count++;
                break;
            } else if(
                DURATION_DIFF(
                    instance->decoder.te_last, subghz_protocol_nero_sketch_const.te_short * 4) <
                subghz_protocol_nero_sketch_const.te_delta) {

                if(instance->header_count > 40) {
                    instance->decoder.parser_step = NeroSketchDecoderStepSaveDuration;
                    instance->decoder.decode_data = 0;
                    instance->decoder.decode_count_bit = 0;
                } else {
                    instance->decoder.parser_step = NeroSketchDecoderStepReset;
                }
            } else {
                instance->decoder.parser_step = NeroSketchDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = NeroSketchDecoderStepReset;
        }
        break;
    case NeroSketchDecoderStepSaveDuration:
        if(level) {
            if(duration >= (subghz_protocol_nero_sketch_const.te_short * 2 +
                            subghz_protocol_nero_sketch_const.te_delta * 2)) {

                instance->decoder.parser_step = NeroSketchDecoderStepReset;
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_nero_sketch_const.min_count_bit_for_found) {
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
                instance->decoder.parser_step = NeroSketchDecoderStepCheckDuration;
            }

        } else {
            instance->decoder.parser_step = NeroSketchDecoderStepReset;
        }
        break;
    case NeroSketchDecoderStepCheckDuration:
        if(!level) {
            if((DURATION_DIFF(
                    instance->decoder.te_last, subghz_protocol_nero_sketch_const.te_short) <
                subghz_protocol_nero_sketch_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_nero_sketch_const.te_long) <
                subghz_protocol_nero_sketch_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = NeroSketchDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(
                     instance->decoder.te_last, subghz_protocol_nero_sketch_const.te_long) <
                 subghz_protocol_nero_sketch_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_nero_sketch_const.te_short) <
                 subghz_protocol_nero_sketch_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = NeroSketchDecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = NeroSketchDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = NeroSketchDecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_nero_sketch_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderNeroSketch* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_nero_sketch_const.min_count_bit_for_found);
}

