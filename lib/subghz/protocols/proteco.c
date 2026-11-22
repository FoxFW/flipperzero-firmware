#include "proteco.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "common.h"
#include "decode_plugin.h"
#include "../encode_plugin.h"

#include "../blocks/custom_btn_i.h"

#define TAG "SubGhzProtocolProteco"

#define PROTECO_GUARD_TE_X2_MIN 37u
#define PROTECO_GUARD_TE_X2_MAX 45u

#define PROTECO_BTN_MASK 0x3u
static const uint8_t subghz_protocol_proteco_btn[4] = {0x2, 0x3, 0x1, 0x4};

static const SubGhzBlockConst subghz_protocol_proteco_const = {
    .te_short = 440,
    .te_long = 880,
    .te_delta = 150,
    .min_count_bit_for_found = 24,
};

struct SubGhzProtocolDecoderProteco {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t te;
    uint32_t guard;
};
SUBGHZ_ASSERT_DECODER_TE_LAYOUT(SubGhzProtocolDecoderProteco);

typedef enum {
    ProtecoDecoderStepReset = 0,
    ProtecoDecoderStepFoundStartBit,
    ProtecoDecoderStepSaveDuration,
    ProtecoDecoderStepCheckDuration,
} ProtecoDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_proteco_decoder = {
    .alloc = subghz_protocol_decoder_proteco_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_proteco_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_proteco_deserialize,
    .get_string = subghz_protocol_decoder_proteco_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_proteco_encoder_ext =
    SUBGHZ_ENCODER_EXTERNAL("enc_proteco");

const SubGhzProtocol subghz_protocol_proteco = {
    .name = SUBGHZ_PROTOCOL_PROTECO_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_proteco_decoder,
    .encoder = &subghz_protocol_proteco_encoder_ext.vtable,
};

void* subghz_protocol_decoder_proteco_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderProteco), &subghz_protocol_proteco);
}

static bool subghz_protocol_proteco_guard_ok(SubGhzProtocolDecoderProteco* instance) {

    uint32_t te = instance->te / (instance->decoder.decode_count_bit * 3 + 1);
    return (instance->guard >= te * PROTECO_GUARD_TE_X2_MIN / 2) &&
           (instance->guard <= te * PROTECO_GUARD_TE_X2_MAX / 2);
}

void subghz_protocol_decoder_proteco_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderProteco* instance = context;
    const SubGhzBlockConst* c = &subghz_protocol_proteco_const;

    switch(instance->decoder.parser_step) {
    case ProtecoDecoderStepReset:
        if((!level) && (duration >= c->te_short * 4)) {
            instance->guard = duration;
            instance->decoder.parser_step = ProtecoDecoderStepFoundStartBit;
        }
        break;
    case ProtecoDecoderStepFoundStartBit:
        if(level && (DURATION_DIFF(duration, c->te_short) < c->te_delta)) {
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->te = duration;
            instance->decoder.parser_step = ProtecoDecoderStepSaveDuration;
        } else {
            instance->decoder.parser_step = ProtecoDecoderStepReset;
        }
        break;
    case ProtecoDecoderStepSaveDuration:
        if(level) {
            instance->decoder.parser_step = ProtecoDecoderStepReset;
            break;
        }
        if(duration >= c->te_short * 4) {

            if((instance->decoder.decode_count_bit == c->min_count_bit_for_found) &&
               subghz_protocol_proteco_guard_ok(instance)) {
                instance->generic.data = instance->decoder.decode_data;
                instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                if(instance->base.callback)
                    instance->base.callback(&instance->base, instance->base.context);
            }
            instance->guard = duration;
            instance->decoder.parser_step = ProtecoDecoderStepFoundStartBit;
            break;
        }
        instance->decoder.te_last = duration;
        instance->te += duration;
        instance->decoder.parser_step = ProtecoDecoderStepCheckDuration;
        break;
    case ProtecoDecoderStepCheckDuration:
        if(level && (DURATION_DIFF(instance->decoder.te_last, c->te_short) < c->te_delta) &&
           (DURATION_DIFF(duration, c->te_long) < c->te_delta)) {
            subghz_protocol_blocks_add_bit(&instance->decoder, 0);
            instance->te += duration;
            instance->decoder.parser_step = ProtecoDecoderStepSaveDuration;
        } else if(
            level && (DURATION_DIFF(instance->decoder.te_last, c->te_long) < c->te_delta) &&
            (DURATION_DIFF(duration, c->te_short) < c->te_delta)) {
            subghz_protocol_blocks_add_bit(&instance->decoder, 1);
            instance->te += duration;
            instance->decoder.parser_step = ProtecoDecoderStepSaveDuration;
        } else {
            instance->decoder.parser_step = ProtecoDecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_proteco_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderProteco* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_proteco_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_proteco_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderProteco* instance = context;

    instance->generic.serial = (uint32_t)(instance->generic.data >> 2) & 0x3FFFFF;
    instance->generic.btn = subghz_protocol_proteco_btn[instance->generic.data & PROTECO_BTN_MASK];

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->generic.btn);
    }
    subghz_custom_btn_set_max(3);

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 3;

    const SubGhzDecodeInput input = {
        .decoder = context,
        .generic = &instance->generic,
        .extra = NULL,
        .extra_count = 0,
        .aux = NULL,
    };
    subghz_decode_plugin_get_string(instance->generic.protocol_name, &input, output);
}
