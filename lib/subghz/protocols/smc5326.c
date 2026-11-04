#include "smc5326.h"

#include "common.h"
#include "../encode_plugin.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolSmc5326"

#define DIP_P 0b11
#define DIP_O 0b10
#define DIP_N 0b00

#define DIP_PATTERN "%c%c%c%c%c%c%c%c"
#define SHOW_DIP_P(dip, check_dip)                         \
    ((((dip >> 0xE) & 0x3) == check_dip) ? '*' : '_'),     \
        ((((dip >> 0xC) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0xA) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x8) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x6) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x4) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x2) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x0) & 0x3) == check_dip) ? '*' : '_')

static const SubGhzBlockConst subghz_protocol_smc5326_const = {
    .te_short = 300,
    .te_long = 900,
    .te_delta = 200,
    .min_count_bit_for_found = 25,
};

struct SubGhzProtocolDecoderSMC5326 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t te;
    uint32_t last_data;
};

struct SubGhzProtocolEncoderSMC5326 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint32_t te;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderSMC5326);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderSMC5326);

typedef enum {
    SMC5326DecoderStepReset = 0,
    SMC5326DecoderStepSaveDuration,
    SMC5326DecoderStepCheckDuration,
} SMC5326DecoderStep;

const SubGhzProtocolDecoder subghz_protocol_smc5326_decoder = {
    .alloc = subghz_protocol_decoder_smc5326_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_smc5326_feed,
    .reset = subghz_protocol_decoder_smc5326_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_smc5326_serialize,
    .deserialize = subghz_protocol_decoder_smc5326_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_smc5326_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_smc5326");

const SubGhzProtocol subghz_protocol_smc5326 = {
    .name = SUBGHZ_PROTOCOL_SMC5326_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 | SubGhzProtocolFlag_315 |
            SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_smc5326_decoder,
    .encoder = &subghz_protocol_smc5326_encoder_ext.vtable,
};

void* subghz_protocol_decoder_smc5326_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderSMC5326), &subghz_protocol_smc5326);
}

void subghz_protocol_decoder_smc5326_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderSMC5326* instance = context;
    instance->decoder.parser_step = SMC5326DecoderStepReset;
    instance->last_data = 0;
}

void subghz_protocol_decoder_smc5326_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderSMC5326* instance = context;

    switch(instance->decoder.parser_step) {
    case SMC5326DecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_smc5326_const.te_short * 24) <
                        subghz_protocol_smc5326_const.te_delta * 12)) {

            instance->decoder.parser_step = SMC5326DecoderStepSaveDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->te = 0;
        }
        break;
    case SMC5326DecoderStepSaveDuration:

        if(level) {
            instance->decoder.te_last = duration;
            instance->te += duration;
            instance->decoder.parser_step = SMC5326DecoderStepCheckDuration;
        }
        break;
    case SMC5326DecoderStepCheckDuration:
        if(!level) {
            if(duration >= ((uint32_t)subghz_protocol_smc5326_const.te_long * 2)) {
                instance->decoder.parser_step = SMC5326DecoderStepSaveDuration;
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_smc5326_const.min_count_bit_for_found) {
                    if((instance->last_data == instance->decoder.decode_data) &&
                       instance->last_data) {
                        instance->te /= (instance->decoder.decode_count_bit * 4 + 1);

                        instance->generic.data = instance->decoder.decode_data;
                        instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                        if(instance->base.callback)
                            instance->base.callback(&instance->base, instance->base.context);
                    }
                    instance->last_data = instance->decoder.decode_data;
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->te = 0;
                break;
            }

            instance->te += duration;

            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_smc5326_const.te_short) <
                subghz_protocol_smc5326_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_smc5326_const.te_long) <
                subghz_protocol_smc5326_const.te_delta * 3)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = SMC5326DecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_smc5326_const.te_long) <
                 subghz_protocol_smc5326_const.te_delta * 3) &&
                (DURATION_DIFF(duration, subghz_protocol_smc5326_const.te_short) <
                 subghz_protocol_smc5326_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = SMC5326DecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = SMC5326DecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = SMC5326DecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_smc5326_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderSMC5326* instance = context;
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
    subghz_protocol_decoder_smc5326_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderSMC5326* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_smc5326_const.min_count_bit_for_found);
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

