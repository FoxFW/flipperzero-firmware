#include "bett.h"

#include "common.h"
#include "../encode_plugin.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolBett"

#define DIP_P 0b11
#define DIP_O 0b10
#define DIP_N 0b00

#define DIP_PATTERN "%c%c%c%c%c%c%c%c%c"
#define SHOW_DIP_P(dip, check_dip)                         \
    ((((dip >> 0x8) >> 0x8) == check_dip) ? '*' : '_'),    \
        ((((dip >> 0xE) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0xC) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0xA) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x8) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x6) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x4) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x2) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x0) & 0x3) == check_dip) ? '*' : '_')

static const SubGhzBlockConst subghz_protocol_bett_const = {
    .te_short = 340,
    .te_long = 2000,
    .te_delta = 150,
    .min_count_bit_for_found = 18,
};

struct SubGhzProtocolDecoderBETT {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderBETT {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderBETT);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderBETT);

typedef enum {
    BETTDecoderStepReset = 0,
    BETTDecoderStepSaveDuration,
    BETTDecoderStepCheckDuration,
} BETTDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_bett_decoder = {
    .alloc = subghz_protocol_decoder_bett_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_bett_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_bett_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_bett_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_bett");

const SubGhzProtocol subghz_protocol_bett = {
    .name = SUBGHZ_PROTOCOL_BETT_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_bett_decoder,
    .encoder = &subghz_protocol_bett_encoder_ext.vtable,
};

void* subghz_protocol_decoder_bett_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderBETT), &subghz_protocol_bett);
}

void subghz_protocol_decoder_bett_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderBETT* instance = context;

    switch(instance->decoder.parser_step) {
    case BETTDecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_bett_const.te_short * 44) <
                        (subghz_protocol_bett_const.te_delta * 15))) {
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = BETTDecoderStepCheckDuration;
        }
        break;
    case BETTDecoderStepSaveDuration:
        if(!level) {
            if(DURATION_DIFF(duration, subghz_protocol_bett_const.te_short * 44) <
               (subghz_protocol_bett_const.te_delta * 15)) {
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_bett_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                } else {
                    instance->decoder.parser_step = BETTDecoderStepReset;
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                break;
            } else {
                if((DURATION_DIFF(duration, subghz_protocol_bett_const.te_short) <
                    subghz_protocol_bett_const.te_delta) ||
                   (DURATION_DIFF(duration, subghz_protocol_bett_const.te_long) <
                    subghz_protocol_bett_const.te_delta * 3)) {
                    instance->decoder.parser_step = BETTDecoderStepCheckDuration;
                } else {
                    instance->decoder.parser_step = BETTDecoderStepReset;
                }
            }
        }
        break;
    case BETTDecoderStepCheckDuration:
        if(level) {
            if(DURATION_DIFF(duration, subghz_protocol_bett_const.te_long) <
               subghz_protocol_bett_const.te_delta * 3) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = BETTDecoderStepSaveDuration;
            } else if(
                DURATION_DIFF(duration, subghz_protocol_bett_const.te_short) <
                subghz_protocol_bett_const.te_delta) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = BETTDecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = BETTDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = BETTDecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_bett_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderBETT* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_bett_const.min_count_bit_for_found);
}

