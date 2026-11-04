#include "gangqi.h"

#include "common.h"
#include "../encode_plugin.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#include "../blocks/custom_btn_i.h"

#define TAG "SubGhzProtocolGangQi"

static const SubGhzBlockConst subghz_protocol_gangqi_const = {
    .te_short = 500,
    .te_long = 1200,
    .te_delta = 200,
    .min_count_bit_for_found = 34,
};

struct SubGhzProtocolDecoderGangQi {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderGangQi {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderGangQi);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderGangQi);

typedef enum {
    GangQiDecoderStepReset = 0,
    GangQiDecoderStepSaveDuration,
    GangQiDecoderStepCheckDuration,
} GangQiDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_gangqi_decoder = {
    .alloc = subghz_protocol_decoder_gangqi_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_gangqi_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_gangqi_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_gangqi_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_gangqi");

const SubGhzProtocol subghz_protocol_gangqi = {
    .name = SUBGHZ_PROTOCOL_GANGQI_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send |
            SubGhzProtocolFlag_Alarms,

    .decoder = &subghz_protocol_gangqi_decoder,
    .encoder = &subghz_protocol_gangqi_encoder_ext.vtable,
};

void* subghz_protocol_decoder_gangqi_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderGangQi), &subghz_protocol_gangqi);
}

void subghz_protocol_decoder_gangqi_feed(void* context, bool level, volatile uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderGangQi* instance = context;

    switch(instance->decoder.parser_step) {
    case GangQiDecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_gangqi_const.te_long * 2) <
                        subghz_protocol_gangqi_const.te_delta * 3)) {

            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = GangQiDecoderStepSaveDuration;
        }
        break;
    case GangQiDecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = GangQiDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = GangQiDecoderStepReset;
        }
        break;
    case GangQiDecoderStepCheckDuration:
        if(!level) {

            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_gangqi_const.te_short) <
                subghz_protocol_gangqi_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_gangqi_const.te_long) <
                subghz_protocol_gangqi_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = GangQiDecoderStepSaveDuration;

            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_gangqi_const.te_long) <
                 subghz_protocol_gangqi_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_gangqi_const.te_short) <
                 subghz_protocol_gangqi_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = GangQiDecoderStepSaveDuration;
            } else if(

                (DURATION_DIFF(duration, subghz_protocol_gangqi_const.te_long * 2) <
                 subghz_protocol_gangqi_const.te_delta * 3)) {

                if((DURATION_DIFF(
                        instance->decoder.te_last, subghz_protocol_gangqi_const.te_short) <
                    subghz_protocol_gangqi_const.te_delta)) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                }
                if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_gangqi_const.te_long) <
                    subghz_protocol_gangqi_const.te_delta)) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                }

                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_gangqi_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->decoder.parser_step = GangQiDecoderStepReset;
            } else {
                instance->decoder.parser_step = GangQiDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = GangQiDecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_gangqi_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderGangQi* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_gangqi_const.min_count_bit_for_found);
}

