#include "intertechno_v3.h"

#include "common.h"
#include "../encode_plugin.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolIntertechnoV3"

#define CH_PATTERN "%c%c%c%c"
#define CNT_TO_CH(ch) \
    (ch & 0x8 ? '1' : '0'), (ch & 0x4 ? '1' : '0'), (ch & 0x2 ? '1' : '0'), (ch & 0x1 ? '1' : '0')

#define INTERTECHNO_V3_DIMMING_COUNT_BIT 36

static const SubGhzBlockConst subghz_protocol_intertechno_v3_const = {
    .te_short = 275,
    .te_long = 1375,
    .te_delta = 150,
    .min_count_bit_for_found = 32,
};

struct SubGhzProtocolDecoderIntertechno_V3 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderIntertechno_V3 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderIntertechno_V3);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderIntertechno_V3);

typedef enum {
    IntertechnoV3DecoderStepReset = 0,
    IntertechnoV3DecoderStepStartSync,
    IntertechnoV3DecoderStepFoundSync,
    IntertechnoV3DecoderStepStartDuration,
    IntertechnoV3DecoderStepSaveDuration,
    IntertechnoV3DecoderStepCheckDuration,
    IntertechnoV3DecoderStepEndDuration,
} IntertechnoV3DecoderStep;

const SubGhzProtocolDecoder subghz_protocol_intertechno_v3_decoder = {
    .alloc = subghz_protocol_decoder_intertechno_v3_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_intertechno_v3_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_intertechno_v3_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_intertechno_v3_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_intertechno_v3");

const SubGhzProtocol subghz_protocol_intertechno_v3 = {
    .name = SUBGHZ_PROTOCOL_INTERTECHNO_V3_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_868 |
            SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_intertechno_v3_decoder,
    .encoder = &subghz_protocol_intertechno_v3_encoder_ext.vtable,
};

void* subghz_protocol_decoder_intertechno_v3_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderIntertechno_V3), &subghz_protocol_intertechno_v3);
}

void subghz_protocol_decoder_intertechno_v3_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderIntertechno_V3* instance = context;
    switch(instance->decoder.parser_step) {
    case IntertechnoV3DecoderStepReset:
        if((!level) &&
           (DURATION_DIFF(duration, subghz_protocol_intertechno_v3_const.te_short * 37) <
            subghz_protocol_intertechno_v3_const.te_delta * 15)) {
            instance->decoder.parser_step = IntertechnoV3DecoderStepStartSync;
        }
        break;
    case IntertechnoV3DecoderStepStartSync:
        if(level && (DURATION_DIFF(duration, subghz_protocol_intertechno_v3_const.te_short) <
                     subghz_protocol_intertechno_v3_const.te_delta)) {
            instance->decoder.parser_step = IntertechnoV3DecoderStepFoundSync;
        } else {
            instance->decoder.parser_step = IntertechnoV3DecoderStepReset;
        }
        break;

    case IntertechnoV3DecoderStepFoundSync:
        if(!level && (DURATION_DIFF(duration, subghz_protocol_intertechno_v3_const.te_short * 10) <
                      subghz_protocol_intertechno_v3_const.te_delta * 3)) {
            instance->decoder.parser_step = IntertechnoV3DecoderStepStartDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
        } else {
            instance->decoder.parser_step = IntertechnoV3DecoderStepReset;
        }
        break;

    case IntertechnoV3DecoderStepStartDuration:
        if(level && (DURATION_DIFF(duration, subghz_protocol_intertechno_v3_const.te_short) <
                     subghz_protocol_intertechno_v3_const.te_delta)) {
            instance->decoder.parser_step = IntertechnoV3DecoderStepSaveDuration;
        } else {
            instance->decoder.parser_step = IntertechnoV3DecoderStepReset;
        }
        break;

    case IntertechnoV3DecoderStepSaveDuration:
        if(!level) {
            if(duration >= (subghz_protocol_intertechno_v3_const.te_short * 11)) {
                instance->decoder.parser_step = IntertechnoV3DecoderStepStartSync;
                if((instance->decoder.decode_count_bit ==
                    subghz_protocol_intertechno_v3_const.min_count_bit_for_found) ||
                   (instance->decoder.decode_count_bit == INTERTECHNO_V3_DIMMING_COUNT_BIT)) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                break;
            }
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = IntertechnoV3DecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = IntertechnoV3DecoderStepReset;
        }
        break;
    case IntertechnoV3DecoderStepCheckDuration:
        if(level) {

            if((DURATION_DIFF(
                    instance->decoder.te_last, subghz_protocol_intertechno_v3_const.te_short) <
                subghz_protocol_intertechno_v3_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_intertechno_v3_const.te_short) <
                subghz_protocol_intertechno_v3_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = IntertechnoV3DecoderStepEndDuration;
            } else if(

                (DURATION_DIFF(
                     instance->decoder.te_last, subghz_protocol_intertechno_v3_const.te_long) <
                 subghz_protocol_intertechno_v3_const.te_delta * 2) &&
                (DURATION_DIFF(duration, subghz_protocol_intertechno_v3_const.te_short) <
                 subghz_protocol_intertechno_v3_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = IntertechnoV3DecoderStepEndDuration;

            } else if(

                (DURATION_DIFF(
                     instance->decoder.te_last, subghz_protocol_intertechno_v3_const.te_short) <
                 subghz_protocol_intertechno_v3_const.te_delta * 2) &&
                (DURATION_DIFF(duration, subghz_protocol_intertechno_v3_const.te_short) <
                 subghz_protocol_intertechno_v3_const.te_delta) &&
                (instance->decoder.decode_count_bit == 27)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = IntertechnoV3DecoderStepEndDuration;

            } else
                instance->decoder.parser_step = IntertechnoV3DecoderStepReset;
        } else {
            instance->decoder.parser_step = IntertechnoV3DecoderStepReset;
        }
        break;

    case IntertechnoV3DecoderStepEndDuration:
        if(!level && ((DURATION_DIFF(duration, subghz_protocol_intertechno_v3_const.te_short) <
                       subghz_protocol_intertechno_v3_const.te_delta) ||
                      (DURATION_DIFF(duration, subghz_protocol_intertechno_v3_const.te_long) <
                       subghz_protocol_intertechno_v3_const.te_delta * 2))) {
            instance->decoder.parser_step = IntertechnoV3DecoderStepStartDuration;
        } else {
            instance->decoder.parser_step = IntertechnoV3DecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_intertechno_v3_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderIntertechno_V3* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }
        if((instance->generic.data_count_bit !=
            subghz_protocol_intertechno_v3_const.min_count_bit_for_found) &&
           (instance->generic.data_count_bit != INTERTECHNO_V3_DIMMING_COUNT_BIT)) {
            FURI_LOG_E(TAG, "Wrong number of bits in key");
            ret = SubGhzProtocolStatusErrorValueBitCount;
            break;
        }
    } while(false);
    return ret;
}

