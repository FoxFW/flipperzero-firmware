#include "ansonic.h"

#include "common.h"
#include "../encode_plugin.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolAnsonic"

#define DIP_PATTERN "%c%c%c%c%c%c%c%c%c%c"
#define CNT_TO_DIP(dip)                                                                     \
    (dip & 0x0800 ? '1' : '0'), (dip & 0x0400 ? '1' : '0'), (dip & 0x0200 ? '1' : '0'),     \
        (dip & 0x0100 ? '1' : '0'), (dip & 0x0080 ? '1' : '0'), (dip & 0x0040 ? '1' : '0'), \
        (dip & 0x0020 ? '1' : '0'), (dip & 0x0010 ? '1' : '0'), (dip & 0x0001 ? '1' : '0'), \
        (dip & 0x0008 ? '1' : '0')

static const SubGhzBlockConst subghz_protocol_ansonic_const = {
    .te_short = 555,
    .te_long = 1111,
    .te_delta = 120,
    .min_count_bit_for_found = 12,
};

struct SubGhzProtocolDecoderAnsonic {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderAnsonic {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderAnsonic);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderAnsonic);

typedef enum {
    AnsonicDecoderStepReset = 0,
    AnsonicDecoderStepFoundStartBit,
    AnsonicDecoderStepSaveDuration,
    AnsonicDecoderStepCheckDuration,
} AnsonicDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_ansonic_decoder = {
    .alloc = subghz_protocol_decoder_ansonic_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_ansonic_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_ansonic_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_ansonic_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_ansonic");

const SubGhzProtocol subghz_protocol_ansonic = {
    .name = SUBGHZ_PROTOCOL_ANSONIC_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_FM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_ansonic_decoder,
    .encoder = &subghz_protocol_ansonic_encoder_ext.vtable,
};

void* subghz_protocol_decoder_ansonic_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderAnsonic), &subghz_protocol_ansonic);
}

void subghz_protocol_decoder_ansonic_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderAnsonic* instance = context;

    switch(instance->decoder.parser_step) {
    case AnsonicDecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_ansonic_const.te_short * 35) <
                        subghz_protocol_ansonic_const.te_delta * 35)) {

            instance->decoder.parser_step = AnsonicDecoderStepFoundStartBit;
        }
        break;
    case AnsonicDecoderStepFoundStartBit:
        if(!level) {
            break;
        } else if(
            DURATION_DIFF(duration, subghz_protocol_ansonic_const.te_short) <
            subghz_protocol_ansonic_const.te_delta) {

            instance->decoder.parser_step = AnsonicDecoderStepSaveDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
        } else {
            instance->decoder.parser_step = AnsonicDecoderStepReset;
        }
        break;
    case AnsonicDecoderStepSaveDuration:
        if(!level) {
            if(duration >= (subghz_protocol_ansonic_const.te_short * 4)) {
                instance->decoder.parser_step = AnsonicDecoderStepFoundStartBit;
                if(instance->decoder.decode_count_bit >=
                   subghz_protocol_ansonic_const.min_count_bit_for_found) {
                    instance->generic.serial = 0x0;
                    instance->generic.btn = 0x0;

                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                break;
            }
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = AnsonicDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = AnsonicDecoderStepReset;
        }
        break;
    case AnsonicDecoderStepCheckDuration:
        if(level) {
            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_ansonic_const.te_short) <
                subghz_protocol_ansonic_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_ansonic_const.te_long) <
                subghz_protocol_ansonic_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = AnsonicDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_ansonic_const.te_long) <
                 subghz_protocol_ansonic_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_ansonic_const.te_short) <
                 subghz_protocol_ansonic_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = AnsonicDecoderStepSaveDuration;
            } else
                instance->decoder.parser_step = AnsonicDecoderStepReset;
        } else {
            instance->decoder.parser_step = AnsonicDecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_ansonic_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderAnsonic* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_ansonic_const.min_count_bit_for_found);
}

