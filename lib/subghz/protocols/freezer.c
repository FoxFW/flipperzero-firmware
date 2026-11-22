#include "freezer.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "common.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"

#define TAG "SubGhzProtocolFreezer"

static const SubGhzBlockConst subghz_protocol_freezer_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 120,
    .min_count_bit_for_found = 24,
};

#define FREEZER_SYMBOL_MIN   120
#define FREEZER_SYMBOL_SPLIT 375
#define FREEZER_SYMBOL_MAX   700

#define FREEZER_GAP_MIN_TE   30
#define FREEZER_GAP_MIN      ((uint32_t)subghz_protocol_freezer_const.te_short * FREEZER_GAP_MIN_TE)
#define FREEZER_GAP_DURATION ((uint32_t)subghz_protocol_freezer_const.te_short * 36)

struct SubGhzProtocolDecoderFreezer {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint64_t last_data;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderFreezer);

struct SubGhzProtocolEncoderFreezer {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderFreezer);

typedef enum {
    FreezerDecoderStepReset = 0,
    FreezerDecoderStepStart,
    FreezerDecoderStepSaveDuration,
    FreezerDecoderStepCheckDuration,
} FreezerDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_freezer_decoder = {
    .alloc = subghz_protocol_decoder_freezer_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_freezer_feed,
    .reset = subghz_protocol_decoder_freezer_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_freezer_deserialize,
    .get_string = subghz_protocol_decoder_freezer_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_freezer_encoder_ext =
    SUBGHZ_ENCODER_EXTERNAL("enc_freezer");

const SubGhzProtocol subghz_protocol_freezer = {
    .name = SUBGHZ_PROTOCOL_FREEZER_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_freezer_decoder,
    .encoder = &subghz_protocol_freezer_encoder_ext.vtable,
};

static void subghz_protocol_freezer_check_remote_controller(SubGhzBlockGeneric* instance) {
    instance->serial = (instance->data >> 4) & 0xFFFFF;
    instance->btn = instance->data & 0xF;
}

void* subghz_protocol_decoder_freezer_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderFreezer* instance = subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderFreezer), &subghz_protocol_freezer);
    instance->last_data = 0;

    return instance;
}

void subghz_protocol_decoder_freezer_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderFreezer* instance = context;
    instance->decoder.parser_step = FreezerDecoderStepReset;
    instance->last_data = 0;
}

static bool subghz_protocol_freezer_is_short(uint32_t duration) {
    return (duration >= FREEZER_SYMBOL_MIN) && (duration <= FREEZER_SYMBOL_SPLIT);
}

static bool subghz_protocol_freezer_is_long(uint32_t duration) {
    return (duration > FREEZER_SYMBOL_SPLIT) && (duration <= FREEZER_SYMBOL_MAX);
}

void subghz_protocol_decoder_freezer_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderFreezer* instance = context;

    switch(instance->decoder.parser_step) {
    case FreezerDecoderStepReset:
        if((!level) && (duration >= FREEZER_GAP_MIN)) {
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = FreezerDecoderStepStart;
        }
        break;

    case FreezerDecoderStepStart:
        if(level && subghz_protocol_freezer_is_short(duration)) {
            instance->decoder.parser_step = FreezerDecoderStepSaveDuration;
        } else {
            instance->decoder.parser_step = FreezerDecoderStepReset;
        }
        break;

    case FreezerDecoderStepSaveDuration:
        if(level) {
            instance->decoder.parser_step = FreezerDecoderStepReset;
            break;
        }
        if(duration >= FREEZER_GAP_MIN) {

            if((instance->decoder.decode_count_bit ==
                subghz_protocol_freezer_const.min_count_bit_for_found) &&
               (instance->decoder.decode_data == instance->last_data) && instance->last_data) {
                instance->generic.data = instance->decoder.decode_data;
                instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                if(instance->base.callback)
                    instance->base.callback(&instance->base, instance->base.context);
            }
            if(instance->decoder.decode_count_bit ==
               subghz_protocol_freezer_const.min_count_bit_for_found) {
                instance->last_data = instance->decoder.decode_data;
            } else {
                instance->last_data = 0;
            }
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = FreezerDecoderStepStart;
            break;
        }
        instance->decoder.te_last = duration;
        instance->decoder.parser_step = FreezerDecoderStepCheckDuration;
        break;

    case FreezerDecoderStepCheckDuration:
        if(!level) {
            instance->decoder.parser_step = FreezerDecoderStepReset;
            break;
        }
        if(subghz_protocol_freezer_is_short(instance->decoder.te_last) &&
           subghz_protocol_freezer_is_long(duration)) {
            subghz_protocol_blocks_add_bit(&instance->decoder, 1);
            instance->decoder.parser_step = FreezerDecoderStepSaveDuration;
        } else if(
            subghz_protocol_freezer_is_long(instance->decoder.te_last) &&
            subghz_protocol_freezer_is_short(duration)) {
            subghz_protocol_blocks_add_bit(&instance->decoder, 0);
            instance->decoder.parser_step = FreezerDecoderStepSaveDuration;
        } else {
            instance->last_data = 0;
            instance->decoder.parser_step = FreezerDecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_freezer_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderFreezer* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_freezer_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_freezer_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderFreezer* instance = context;
    subghz_protocol_freezer_check_remote_controller(&instance->generic);
    const SubGhzDecodeInput input = {
        .generic = &instance->generic,
    };
    subghz_decode_plugin_get_string("Freezer", &input, output);
}
