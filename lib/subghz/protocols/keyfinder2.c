#include "keyfinder2.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "common.h"
#include "../encode_plugin.h"

#define TAG "SubGhzProtocolKeyFinder2"

// Frame is btn(3) | 10011 | btn(3): the button code in 3 wide symbols, a constant, then
// the same code again in narrow bits. Frames repeat back to back with no gap.
#define KEYFINDER2_TE_WIDE_SHORT 1500
#define KEYFINDER2_TE_WIDE_LONG  2500
#define KEYFINDER2_TE_WIDE_DELTA 350
#define KEYFINDER2_HEADER_BITS   3

static const SubGhzBlockConst subghz_protocol_keyfinder2_const = {
    .te_short = 350,
    .te_long = 1020,
    .te_delta = 150,
    .min_count_bit_for_found = 11,
};

struct SubGhzProtocolDecoderKeyFinder2 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderKeyFinder2);

struct SubGhzProtocolEncoderKeyFinder2 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderKeyFinder2);

typedef enum {
    KeyFinder2DecoderStepReset = 0,
    KeyFinder2DecoderStepSaveWide,
    KeyFinder2DecoderStepCheckWide,
    KeyFinder2DecoderStepSaveDuration,
    KeyFinder2DecoderStepCheckDuration,
} KeyFinder2DecoderStep;

const SubGhzProtocolDecoder subghz_protocol_keyfinder2_decoder = {
    .alloc = subghz_protocol_decoder_keyfinder2_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_keyfinder2_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_keyfinder2_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_keyfinder2_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_keyfinder2");

const SubGhzProtocol subghz_protocol_keyfinder2 = {
    .name = SUBGHZ_PROTOCOL_KEYFINDER2_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send |
            SubGhzProtocolFlag_Sensors,

    .decoder = &subghz_protocol_keyfinder2_decoder,
    .encoder = &subghz_protocol_keyfinder2_encoder_ext.vtable,
};

/**
 * Generating an upload from data.
 * @param context Pointer to a SubGhzProtocolEncoderKeyFinder2 instance
 * @return true Always; this encoder has no failure path
 */
/**
 * Analysis of received data
 * @param instance Pointer to a SubGhzBlockGeneric* instance
 */
void* subghz_protocol_decoder_keyfinder2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderKeyFinder2), &subghz_protocol_keyfinder2);
}

void subghz_protocol_decoder_keyfinder2_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderKeyFinder2* instance = context;

    switch(instance->decoder.parser_step) {
    case KeyFinder2DecoderStepReset:
        if((level) &&
           ((DURATION_DIFF(duration, KEYFINDER2_TE_WIDE_SHORT) < KEYFINDER2_TE_WIDE_DELTA) ||
            (DURATION_DIFF(duration, KEYFINDER2_TE_WIDE_LONG) < KEYFINDER2_TE_WIDE_DELTA))) {
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = KeyFinder2DecoderStepCheckWide;
        }
        break;
    case KeyFinder2DecoderStepSaveWide:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = KeyFinder2DecoderStepCheckWide;
        } else {
            instance->decoder.parser_step = KeyFinder2DecoderStepReset;
        }
        break;
    case KeyFinder2DecoderStepCheckWide:
        if(!level) {
            if((DURATION_DIFF(instance->decoder.te_last, KEYFINDER2_TE_WIDE_SHORT) <
                KEYFINDER2_TE_WIDE_DELTA) &&
               (DURATION_DIFF(duration, KEYFINDER2_TE_WIDE_LONG) < KEYFINDER2_TE_WIDE_DELTA)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, KEYFINDER2_TE_WIDE_LONG) <
                 KEYFINDER2_TE_WIDE_DELTA) &&
                (DURATION_DIFF(duration, KEYFINDER2_TE_WIDE_SHORT) < KEYFINDER2_TE_WIDE_DELTA)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
            } else {
                instance->decoder.parser_step = KeyFinder2DecoderStepReset;
                break;
            }
            instance->decoder.parser_step =
                (instance->decoder.decode_count_bit == KEYFINDER2_HEADER_BITS) ?
                    KeyFinder2DecoderStepSaveDuration :
                    KeyFinder2DecoderStepSaveWide;
        } else {
            instance->decoder.parser_step = KeyFinder2DecoderStepReset;
        }
        break;
    case KeyFinder2DecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = KeyFinder2DecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = KeyFinder2DecoderStepReset;
        }
        break;
    case KeyFinder2DecoderStepCheckDuration:
        if(!level) {
            // Bit 1 is short and long timing = 350us HIGH (te_last) and 1020us LOW
            if((DURATION_DIFF(
                    instance->decoder.te_last, subghz_protocol_keyfinder2_const.te_short) <
                subghz_protocol_keyfinder2_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_keyfinder2_const.te_long) <
                subghz_protocol_keyfinder2_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                // Bit 0 is long and short timing = 1020us HIGH (te_last) and 350us LOW
            } else if(
                (DURATION_DIFF(
                     instance->decoder.te_last, subghz_protocol_keyfinder2_const.te_long) <
                 subghz_protocol_keyfinder2_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_keyfinder2_const.te_short) <
                 subghz_protocol_keyfinder2_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
            } else {
                instance->decoder.parser_step = KeyFinder2DecoderStepReset;
                break;
            }

            if(instance->decoder.decode_count_bit ==
               subghz_protocol_keyfinder2_const.min_count_bit_for_found) {
                instance->generic.data = instance->decoder.decode_data;
                instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                if(instance->base.callback)
                    instance->base.callback(&instance->base, instance->base.context);
                instance->decoder.parser_step = KeyFinder2DecoderStepReset;
            } else {
                instance->decoder.parser_step = KeyFinder2DecoderStepSaveDuration;
            }
        } else {
            instance->decoder.parser_step = KeyFinder2DecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_keyfinder2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderKeyFinder2* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_keyfinder2_const.min_count_bit_for_found);
}

