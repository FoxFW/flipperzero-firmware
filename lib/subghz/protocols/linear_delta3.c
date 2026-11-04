#include "linear_delta3.h"

#include "common.h"
#include "../encode_plugin.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolLinearDelta3"

#define DIP_PATTERN "%c%c%c%c%c%c%c%c"
#define DATA_TO_DIP(dip)                                                                    \
    (dip & 0x0080 ? '0' : '1'), (dip & 0x0040 ? '0' : '1'), (dip & 0x0020 ? '0' : '1'),     \
        (dip & 0x0010 ? '0' : '1'), (dip & 0x0008 ? '0' : '1'), (dip & 0x0004 ? '0' : '1'), \
        (dip & 0x0002 ? '0' : '1'), (dip & 0x0001 ? '0' : '1')

static const SubGhzBlockConst subghz_protocol_linear_delta3_const = {
    .te_short = 500,
    .te_long = 2000,
    .te_delta = 150,
    .min_count_bit_for_found = 8,
};

struct SubGhzProtocolDecoderLinearDelta3 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t last_data;
};

struct SubGhzProtocolEncoderLinearDelta3 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderLinearDelta3);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderLinearDelta3);

typedef enum {
    LinearDecoderStepReset = 0,
    LinearDecoderStepSaveDuration,
    LinearDecoderStepCheckDuration,
} LinearDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_linear_delta3_decoder = {
    .alloc = subghz_protocol_decoder_linear_delta3_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_linear_delta3_feed,
    .reset = subghz_protocol_decoder_linear_delta3_reset,

    .get_hash_data = subghz_protocol_decoder_linear_delta3_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_linear_delta3_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_linear_delta3_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_linear_delta3");

const SubGhzProtocol subghz_protocol_linear_delta3 = {
    .name = SUBGHZ_PROTOCOL_LINEAR_DELTA3_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_linear_delta3_decoder,
    .encoder = &subghz_protocol_linear_delta3_encoder_ext.vtable,
};

void* subghz_protocol_decoder_linear_delta3_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderLinearDelta3), &subghz_protocol_linear_delta3);
}

void subghz_protocol_decoder_linear_delta3_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderLinearDelta3* instance = context;
    instance->decoder.parser_step = LinearDecoderStepReset;
    instance->last_data = 0;
}

void subghz_protocol_decoder_linear_delta3_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderLinearDelta3* instance = context;
    switch(instance->decoder.parser_step) {
    case LinearDecoderStepReset:
        if((!level) &&
           (DURATION_DIFF(duration, subghz_protocol_linear_delta3_const.te_short * 70) <
            subghz_protocol_linear_delta3_const.te_delta * 24)) {

            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = LinearDecoderStepSaveDuration;
        }
        break;
    case LinearDecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = LinearDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = LinearDecoderStepReset;
        }
        break;
    case LinearDecoderStepCheckDuration:
        if(!level) {
            if(duration >= (subghz_protocol_linear_delta3_const.te_short * 10)) {
                instance->decoder.parser_step = LinearDecoderStepReset;
                if(DURATION_DIFF(
                       instance->decoder.te_last, subghz_protocol_linear_delta3_const.te_short) <
                   subghz_protocol_linear_delta3_const.te_delta) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                } else if(
                    DURATION_DIFF(
                        instance->decoder.te_last, subghz_protocol_linear_delta3_const.te_long) <
                    subghz_protocol_linear_delta3_const.te_delta) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                }
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_linear_delta3_const.min_count_bit_for_found) {
                    if((instance->last_data == instance->decoder.decode_data) &&
                       instance->last_data) {
                        instance->generic.serial = 0x0;
                        instance->generic.btn = 0x0;

                        instance->generic.data = instance->decoder.decode_data;
                        instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                        if(instance->base.callback)
                            instance->base.callback(&instance->base, instance->base.context);
                    }
                    instance->decoder.parser_step = LinearDecoderStepSaveDuration;
                    instance->last_data = instance->decoder.decode_data;
                }
                break;
            }

            if((DURATION_DIFF(
                    instance->decoder.te_last, subghz_protocol_linear_delta3_const.te_short) <
                subghz_protocol_linear_delta3_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_linear_delta3_const.te_short * 7) <
                subghz_protocol_linear_delta3_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = LinearDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(
                     instance->decoder.te_last, subghz_protocol_linear_delta3_const.te_long) <
                 subghz_protocol_linear_delta3_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_linear_delta3_const.te_long) <
                 subghz_protocol_linear_delta3_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = LinearDecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = LinearDecoderStepReset;
            }

        } else {
            instance->decoder.parser_step = LinearDecoderStepReset;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_linear_delta3_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderLinearDelta3* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8));
}

SubGhzProtocolStatus subghz_protocol_decoder_linear_delta3_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderLinearDelta3* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_linear_delta3_const.min_count_bit_for_found);
}

