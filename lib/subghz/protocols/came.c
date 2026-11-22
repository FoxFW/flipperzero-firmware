#include "came.h"

#include "common.h"
#include "../encode_plugin.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolCame"

#define CAME_12_COUNT_BIT    12
#define CAME_24_COUNT_BIT    24
#define PRASTEL_25_COUNT_BIT 25
#define PRASTEL_42_COUNT_BIT 42
#define PRASTEL_NAME         "Prastel"
#define AIRFORCE_COUNT_BIT   18
#define AIRFORCE_NAME        "Airforce"

#define CAME_12_COUNT_BIT      12
#define CAME_12_FOREIGN_TE_MIN 340
#define CAME_12_GAP_TE_MIN     42
#define CAME_12_GAP_TE_MAX     52

static const SubGhzBlockConst subghz_protocol_came_const = {
    .te_short = 320,
    .te_long = 640,
    .te_delta = 150,
    .min_count_bit_for_found = 12,
};

struct SubGhzProtocolDecoderCame {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint32_t header_te;
    uint32_t te;
};

struct SubGhzProtocolEncoderCame {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderCame);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderCame);

typedef enum {
    CameDecoderStepReset = 0,
    CameDecoderStepFoundStartBit,
    CameDecoderStepSaveDuration,
    CameDecoderStepCheckDuration,
} CameDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_came_decoder = {
    .alloc = subghz_protocol_decoder_came_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_came_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_came_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_came_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_came");

const SubGhzProtocol subghz_protocol_came = {
    .name = SUBGHZ_PROTOCOL_CAME_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_came_decoder,
    .encoder = &subghz_protocol_came_encoder_ext.vtable,
};

void* subghz_protocol_decoder_came_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderCame), &subghz_protocol_came);
}

static bool subghz_protocol_came_key_is_degenerate(uint64_t data, uint8_t bits) {
    if(bits == 0U) return true;
    if(bits > 64U) bits = 64U;
    uint64_t mask = (bits == 64U) ? UINT64_MAX : ((1ULL << bits) - 1ULL);
    data &= mask;
    if((data == 0U) || (data == mask)) return true;
    if((data == (0xAAAAAAAAAAAAAAAAULL & mask)) || (data == (0x5555555555555555ULL & mask)))
        return true;
    return false;
}

static bool subghz_protocol_came_payload_ok(uint64_t data, uint8_t bits, uint32_t header_te) {
    if(subghz_protocol_came_key_is_degenerate(data, bits)) return false;
    if(bits != CAME_12_COUNT_BIT) return true;
    if(header_te < (uint32_t)subghz_protocol_came_const.te_short * 41U) return false;
    uint8_t open_nibbles = 0U;
    for(uint8_t i = 0U; i < 3U; i++) {
        uint8_t nibble = (uint8_t)((data >> (4U * i)) & 0x0FU);
        if((nibble == 0x0AU) || (nibble == 0x05U)) open_nibbles++;
    }
    return open_nibbles < 2U;
}

static bool
    subghz_protocol_came_is_foreign_12bit(SubGhzProtocolDecoderCame* instance, uint32_t gap) {
    if(instance->decoder.decode_count_bit != CAME_12_COUNT_BIT) {
        return false;
    }
    uint32_t te = instance->te / (instance->decoder.decode_count_bit * 3 + 1);
    if(te < CAME_12_FOREIGN_TE_MIN) {
        return false;
    }
    uint32_t gap_te = gap / te;
    return (gap_te < CAME_12_GAP_TE_MIN) || (gap_te > CAME_12_GAP_TE_MAX);
}

void subghz_protocol_decoder_came_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderCame* instance = context;
    switch(instance->decoder.parser_step) {
    case CameDecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_came_const.te_short * 56) <
                        subghz_protocol_came_const.te_delta * 63)) {
            instance->header_te = duration;
            instance->decoder.parser_step = CameDecoderStepFoundStartBit;
        }
        break;
    case CameDecoderStepFoundStartBit:
        if(!level) {
            break;
        } else if(
            DURATION_DIFF(duration, subghz_protocol_came_const.te_short) <
            subghz_protocol_came_const.te_delta) {

            instance->decoder.parser_step = CameDecoderStepSaveDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->te = duration;
        } else {
            instance->decoder.parser_step = CameDecoderStepReset;
        }
        break;
    case CameDecoderStepSaveDuration:
        if(!level) {
            if(duration >= (subghz_protocol_came_const.te_short * 4)) {
                instance->decoder.parser_step = CameDecoderStepFoundStartBit;
                if(((instance->decoder.decode_count_bit ==
                    subghz_protocol_came_const.min_count_bit_for_found) ||
                   (instance->decoder.decode_count_bit == AIRFORCE_COUNT_BIT) ||
                   (instance->decoder.decode_count_bit == PRASTEL_25_COUNT_BIT) ||
                   (instance->decoder.decode_count_bit == PRASTEL_42_COUNT_BIT) ||
                   (instance->decoder.decode_count_bit == CAME_24_COUNT_BIT)) &&
                   !subghz_protocol_came_is_foreign_12bit(instance, duration)) {
                    if(subghz_protocol_came_payload_ok(
                           instance->decoder.decode_data,
                           instance->decoder.decode_count_bit,
                           instance->header_te)) {
                        instance->generic.serial = 0x0;
                        instance->generic.btn = 0x0;

                        instance->generic.data = instance->decoder.decode_data;
                        instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                        if(instance->base.callback)
                            instance->base.callback(&instance->base, instance->base.context);
                    }
                }
                break;
            }
            instance->decoder.te_last = duration;
            instance->te += duration;
            instance->decoder.parser_step = CameDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = CameDecoderStepReset;
        }
        break;
    case CameDecoderStepCheckDuration:
        if(level) {
            instance->te += duration;
            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_came_const.te_short) <
                subghz_protocol_came_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_came_const.te_long) <
                subghz_protocol_came_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = CameDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_came_const.te_long) <
                 subghz_protocol_came_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_came_const.te_short) <
                 subghz_protocol_came_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = CameDecoderStepSaveDuration;
            } else
                instance->decoder.parser_step = CameDecoderStepReset;
        } else {
            instance->decoder.parser_step = CameDecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_came_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderCame* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }
        if(instance->generic.data_count_bit > PRASTEL_42_COUNT_BIT) {
            FURI_LOG_E(TAG, "Wrong number of bits in key");
            ret = SubGhzProtocolStatusErrorValueBitCount;
            break;
        }
    } while(false);
    return ret;
}

