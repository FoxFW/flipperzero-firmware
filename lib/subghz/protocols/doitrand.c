#include "doitrand.h"

#include "common.h"
#include "../encode_plugin.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolDoitrand"

#define DIP_PATTERN "%c%c%c%c%c%c%c%c%c%c"
#define CNT_TO_DIP(dip)                                                                     \
    (dip & 0x0001 ? '1' : '0'), (dip & 0x0100 ? '1' : '0'), (dip & 0x0080 ? '1' : '0'),     \
        (dip & 0x0040 ? '1' : '0'), (dip & 0x0020 ? '1' : '0'), (dip & 0x1000 ? '1' : '0'), \
        (dip & 0x0800 ? '1' : '0'), (dip & 0x0400 ? '1' : '0'), (dip & 0x0200 ? '1' : '0'), \
        (dip & 0x0002 ? '1' : '0')

static const SubGhzBlockConst subghz_protocol_doitrand_const = {
    .te_short = 400,
    .te_long = 1100,
    .te_delta = 150,
    .min_count_bit_for_found = 37,
};

struct SubGhzProtocolDecoderDoitrand {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderDoitrand {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderDoitrand);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderDoitrand);

typedef enum {
    DoitrandDecoderStepReset = 0,
    DoitrandDecoderStepFoundStartBit,
    DoitrandDecoderStepSaveDuration,
    DoitrandDecoderStepCheckDuration,
} DoitrandDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_doitrand_decoder = {
    .alloc = subghz_protocol_decoder_doitrand_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_doitrand_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_doitrand_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_doitrand_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_doitrand");

const SubGhzProtocol subghz_protocol_doitrand = {
    .name = SUBGHZ_PROTOCOL_DOITRAND_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_doitrand_decoder,
    .encoder = &subghz_protocol_doitrand_encoder_ext.vtable,
};

void* subghz_protocol_decoder_doitrand_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderDoitrand), &subghz_protocol_doitrand);
}

void subghz_protocol_decoder_doitrand_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderDoitrand* instance = context;

    switch(instance->decoder.parser_step) {
    case DoitrandDecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_doitrand_const.te_short * 62) <
                        subghz_protocol_doitrand_const.te_delta * 30)) {

            instance->decoder.parser_step = DoitrandDecoderStepFoundStartBit;
        }
        break;
    case DoitrandDecoderStepFoundStartBit:
        if(level && (DURATION_DIFF(duration, (subghz_protocol_doitrand_const.te_short * 2)) <
                     subghz_protocol_doitrand_const.te_delta * 3)) {

            instance->decoder.parser_step = DoitrandDecoderStepSaveDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
        } else {
            instance->decoder.parser_step = DoitrandDecoderStepReset;
        }
        break;
    case DoitrandDecoderStepSaveDuration:
        if(!level) {
            if(duration >= ((uint32_t)subghz_protocol_doitrand_const.te_short * 10 +
                            subghz_protocol_doitrand_const.te_delta)) {
                instance->decoder.parser_step = DoitrandDecoderStepFoundStartBit;
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_doitrand_const.min_count_bit_for_found) {
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
                instance->decoder.parser_step = DoitrandDecoderStepCheckDuration;
            }
        }
        break;
    case DoitrandDecoderStepCheckDuration:
        if(level) {
            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_doitrand_const.te_short) <
                subghz_protocol_doitrand_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_doitrand_const.te_long) <
                subghz_protocol_doitrand_const.te_delta * 3)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = DoitrandDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_doitrand_const.te_long) <
                 subghz_protocol_doitrand_const.te_delta * 3) &&
                (DURATION_DIFF(duration, subghz_protocol_doitrand_const.te_short) <
                 subghz_protocol_doitrand_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = DoitrandDecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = DoitrandDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = DoitrandDecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_doitrand_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderDoitrand* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_doitrand_const.min_count_bit_for_found);
}

