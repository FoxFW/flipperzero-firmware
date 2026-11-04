#include "clemsa.h"

#include "common.h"
#include "../encode_plugin.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolClemsa"

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

static const SubGhzBlockConst subghz_protocol_clemsa_const = {
    .te_short = 385,
    .te_long = 2695,
    .te_delta = 150,
    .min_count_bit_for_found = 18,
};

struct SubGhzProtocolDecoderClemsa {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderClemsa {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderClemsa);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderClemsa);

typedef enum {
    ClemsaDecoderStepReset = 0,
    ClemsaDecoderStepSaveDuration,
    ClemsaDecoderStepCheckDuration,
} ClemsaDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_clemsa_decoder = {
    .alloc = subghz_protocol_decoder_clemsa_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_clemsa_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_clemsa_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_clemsa_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_clemsa");

const SubGhzProtocol subghz_protocol_clemsa = {
    .name = SUBGHZ_PROTOCOL_CLEMSA_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_clemsa_decoder,
    .encoder = &subghz_protocol_clemsa_encoder_ext.vtable,
};

void* subghz_protocol_decoder_clemsa_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderClemsa), &subghz_protocol_clemsa);
}

void subghz_protocol_decoder_clemsa_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderClemsa* instance = context;

    switch(instance->decoder.parser_step) {
    case ClemsaDecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_clemsa_const.te_short * 51) <
                        subghz_protocol_clemsa_const.te_delta * 25)) {
            instance->decoder.parser_step = ClemsaDecoderStepSaveDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
        }
        break;

    case ClemsaDecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = ClemsaDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = ClemsaDecoderStepReset;
        }
        break;

    case ClemsaDecoderStepCheckDuration:
        if(!level) {
            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_clemsa_const.te_short) <
                subghz_protocol_clemsa_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_clemsa_const.te_long) <
                subghz_protocol_clemsa_const.te_delta * 3)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = ClemsaDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_clemsa_const.te_long) <
                 subghz_protocol_clemsa_const.te_delta * 3) &&
                (DURATION_DIFF(duration, subghz_protocol_clemsa_const.te_short) <
                 subghz_protocol_clemsa_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = ClemsaDecoderStepSaveDuration;
            } else if(
                DURATION_DIFF(duration, subghz_protocol_clemsa_const.te_short * 51) <
                subghz_protocol_clemsa_const.te_delta * 25) {
                if(DURATION_DIFF(instance->decoder.te_last, subghz_protocol_clemsa_const.te_short) <
                   subghz_protocol_clemsa_const.te_delta) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                } else if(
                    DURATION_DIFF(instance->decoder.te_last, subghz_protocol_clemsa_const.te_long) <
                    subghz_protocol_clemsa_const.te_delta * 3) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                } else {
                    instance->decoder.parser_step = ClemsaDecoderStepReset;
                }

                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_clemsa_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.parser_step = ClemsaDecoderStepSaveDuration;
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;

            } else {
                instance->decoder.parser_step = ClemsaDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = ClemsaDecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_clemsa_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderClemsa* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_clemsa_const.min_count_bit_for_found);
}

