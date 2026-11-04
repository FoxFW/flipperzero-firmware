#include "legrand.h"

#include "common.h"
#include "../encode_plugin.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolLegrand"

static const SubGhzBlockConst subghz_protocol_legrand_const = {
    .te_short = 375,
    .te_long = 1125,
    .te_delta = 150,
    .min_count_bit_for_found = 18,
};

struct SubGhzProtocolDecoderLegrand {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t te;
    uint32_t last_data;
};

struct SubGhzProtocolEncoderLegrand {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint32_t te;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderLegrand);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderLegrand);

typedef enum {
    LegrandDecoderStepReset = 0,
    LegrandDecoderStepFirstBit,
    LegrandDecoderStepSaveDuration,
    LegrandDecoderStepCheckDuration,
} LegrandDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_legrand_decoder = {
    .alloc = subghz_protocol_decoder_legrand_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_legrand_feed,
    .reset = subghz_protocol_decoder_legrand_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_legrand_serialize,
    .deserialize = subghz_protocol_decoder_legrand_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_legrand_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_legrand");

const SubGhzProtocol subghz_protocol_legrand = {
    .name = SUBGHZ_PROTOCOL_LEGRAND_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send |
            SubGhzProtocolFlag_Sensors,

    .decoder = &subghz_protocol_legrand_decoder,
    .encoder = &subghz_protocol_legrand_encoder_ext.vtable,
};

void* subghz_protocol_decoder_legrand_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderLegrand), &subghz_protocol_legrand);
}

void subghz_protocol_decoder_legrand_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderLegrand* instance = context;
    instance->decoder.parser_step = LegrandDecoderStepReset;
    instance->last_data = 0;
}

void subghz_protocol_decoder_legrand_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderLegrand* instance = context;

    switch(instance->decoder.parser_step) {
    case LegrandDecoderStepReset:
        if(!level && DURATION_DIFF(duration, subghz_protocol_legrand_const.te_short * 16) <
                         subghz_protocol_legrand_const.te_delta * 8) {
            instance->decoder.parser_step = LegrandDecoderStepFirstBit;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->te = 0;
        }
        break;
    case LegrandDecoderStepFirstBit:
        if(level) {
            if(DURATION_DIFF(duration, subghz_protocol_legrand_const.te_short) <
               subghz_protocol_legrand_const.te_delta) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->te += duration * 4;
            }

            if(DURATION_DIFF(duration, subghz_protocol_legrand_const.te_long) <
               subghz_protocol_legrand_const.te_delta * 3) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->te += duration / 3 * 4;
            }

            if(instance->decoder.decode_count_bit > 0) {

                instance->decoder.parser_step = LegrandDecoderStepSaveDuration;
                break;
            }
        }

        instance->decoder.parser_step = LegrandDecoderStepReset;
        break;
    case LegrandDecoderStepSaveDuration:
        if(!level) {
            instance->decoder.te_last = duration;
            instance->te += duration;
            instance->decoder.parser_step = LegrandDecoderStepCheckDuration;
            break;
        }

        instance->decoder.parser_step = LegrandDecoderStepReset;
        break;
    case LegrandDecoderStepCheckDuration:
        if(level) {
            uint8_t found = 0;

            if(DURATION_DIFF(instance->decoder.te_last, subghz_protocol_legrand_const.te_long) <
                   subghz_protocol_legrand_const.te_delta * 3 &&
               DURATION_DIFF(duration, subghz_protocol_legrand_const.te_short) <
                   subghz_protocol_legrand_const.te_delta) {
                found = 1;
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
            }

            if(DURATION_DIFF(instance->decoder.te_last, subghz_protocol_legrand_const.te_short) <
                   subghz_protocol_legrand_const.te_delta &&
               DURATION_DIFF(duration, subghz_protocol_legrand_const.te_long) <
                   subghz_protocol_legrand_const.te_delta * 3) {
                found = 1;
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
            }

            if(found) {
                instance->te += duration;

                if(instance->decoder.decode_count_bit <
                   subghz_protocol_legrand_const.min_count_bit_for_found) {
                    instance->decoder.parser_step = LegrandDecoderStepSaveDuration;
                    break;
                }

                if(instance->last_data && (instance->last_data == instance->decoder.decode_data)) {
                    instance->te /= instance->decoder.decode_count_bit * 4;

                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                    if(instance->base.callback) {
                        instance->base.callback(&instance->base, instance->base.context);
                    }
                }
                instance->last_data = instance->decoder.decode_data;

            }
        }

        instance->decoder.parser_step = LegrandDecoderStepReset;
        break;
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_legrand_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderLegrand* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if((ret == SubGhzProtocolStatusOk) &&
       !flipper_format_write_uint32(flipper_format, "TE", &instance->te, 1)) {
        FURI_LOG_E(TAG, "Unable to add TE");
        ret = SubGhzProtocolStatusErrorParserTe;
    }
    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_legrand_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderLegrand* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_legrand_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        if(!flipper_format_read_uint32(flipper_format, "TE", (uint32_t*)&instance->te, 1)) {
            FURI_LOG_E(TAG, "Missing TE");
            ret = SubGhzProtocolStatusErrorParserTe;
            break;
        }
    } while(false);

    return ret;
}

