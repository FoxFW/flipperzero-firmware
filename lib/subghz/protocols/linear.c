#include "linear.h"

#include "common.h"
#include "../encode_plugin.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolLinear"

#define DIP_PATTERN "%c%c%c%c%c%c%c%c%c%c"
#define DATA_TO_DIP(dip)                                                                    \
    (dip & 0x0200 ? '1' : '0'), (dip & 0x0100 ? '1' : '0'), (dip & 0x0080 ? '1' : '0'),     \
        (dip & 0x0040 ? '1' : '0'), (dip & 0x0020 ? '1' : '0'), (dip & 0x0010 ? '1' : '0'), \
        (dip & 0x0008 ? '1' : '0'), (dip & 0x0004 ? '1' : '0'), (dip & 0x0002 ? '1' : '0'), \
        (dip & 0x0001 ? '1' : '0')

static const SubGhzBlockConst subghz_protocol_linear_const = {
    .te_short = 500,
    .te_long = 1500,
    .te_delta = 350,
    .min_count_bit_for_found = 10,
};

struct SubGhzProtocolDecoderLinear {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderLinear {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderLinear);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderLinear);

typedef enum {
    LinearDecoderStepReset = 0,
    LinearDecoderStepSaveDuration,
    LinearDecoderStepCheckDuration,
} LinearDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_linear_decoder = {
    .alloc = subghz_protocol_decoder_linear_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_linear_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_linear_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_linear_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_linear");

const SubGhzProtocol subghz_protocol_linear = {
    .name = SUBGHZ_PROTOCOL_LINEAR_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_linear_decoder,
    .encoder = &subghz_protocol_linear_encoder_ext.vtable,
};

void* subghz_protocol_decoder_linear_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderLinear), &subghz_protocol_linear);
}

void subghz_protocol_decoder_linear_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderLinear* instance = context;
    switch(instance->decoder.parser_step) {
    case LinearDecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_linear_const.te_short * 42) <
                        subghz_protocol_linear_const.te_delta * 15)) {

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
            if(duration >= (subghz_protocol_linear_const.te_short * 5)) {
                instance->decoder.parser_step = LinearDecoderStepReset;

                if(DURATION_DIFF(duration, subghz_protocol_linear_const.te_short * 42) >
                   subghz_protocol_linear_const.te_delta * 15) {
                    break;
                }
                if(DURATION_DIFF(instance->decoder.te_last, subghz_protocol_linear_const.te_short) <
                   subghz_protocol_linear_const.te_delta) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                } else if(
                    DURATION_DIFF(instance->decoder.te_last, subghz_protocol_linear_const.te_long) <
                    subghz_protocol_linear_const.te_delta) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                }
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_linear_const.min_count_bit_for_found) {
                    instance->generic.serial = 0x0;
                    instance->generic.btn = 0x0;

                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                break;
            }

            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_linear_const.te_short) <
                subghz_protocol_linear_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_linear_const.te_long) <
                subghz_protocol_linear_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = LinearDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_linear_const.te_long) <
                 subghz_protocol_linear_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_linear_const.te_short) <
                 subghz_protocol_linear_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
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

SubGhzProtocolStatus
    subghz_protocol_decoder_linear_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderLinear* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_linear_const.min_count_bit_for_found);
}

