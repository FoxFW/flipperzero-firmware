#include "sti3353.h"
#include "common.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolSTI3353"

#define STI3353_GUARD_MIN 2800U
#define STI3353_GUARD_MAX 10000U

static const SubGhzBlockConst subghz_protocol_sti3353_const = {
    .te_short = 200,
    .te_long = 600,
    .te_delta = 150,
    .min_count_bit_for_found = 48,
};

struct SubGhzProtocolDecoderSTI3353 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint64_t last_data;
};

typedef enum {
    STI3353DecoderStepReset = 0,
    STI3353DecoderStepSaveDuration,
    STI3353DecoderStepCheckDuration,
} STI3353DecoderStep;

const SubGhzProtocolDecoder subghz_protocol_sti3353_decoder = {
    .alloc = subghz_protocol_decoder_sti3353_alloc,
    .free = subghz_protocol_decoder_sti3353_free,
    .feed = subghz_protocol_decoder_sti3353_feed,
    .reset = subghz_protocol_decoder_sti3353_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_sti3353_serialize,
    .deserialize = subghz_protocol_decoder_sti3353_deserialize,
    .get_string = subghz_protocol_decoder_sti3353_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_sti3353_encoder_ext =
    SUBGHZ_ENCODER_EXTERNAL("enc_sti3353");

const SubGhzProtocol subghz_protocol_sti3353 = {
    .name = SUBGHZ_PROTOCOL_STI3353_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_sti3353_decoder,
    .encoder = &subghz_protocol_sti3353_encoder_ext.vtable,
};

void* subghz_protocol_decoder_sti3353_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderSTI3353* instance = malloc(sizeof(SubGhzProtocolDecoderSTI3353));
    memset(instance, 0, sizeof(SubGhzProtocolDecoderSTI3353));
    instance->base.protocol = &subghz_protocol_sti3353;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_sti3353_free(void* context) {
    furi_assert(context);
    free(context);
}

void subghz_protocol_decoder_sti3353_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderSTI3353* instance = context;
    instance->decoder.parser_step = STI3353DecoderStepReset;
    instance->last_data = 0;
}

void subghz_protocol_decoder_sti3353_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderSTI3353* instance = context;
    const uint32_t ts = subghz_protocol_sti3353_const.te_short;
    const uint32_t tl = subghz_protocol_sti3353_const.te_long;
    const uint32_t td = subghz_protocol_sti3353_const.te_delta;

    switch(instance->decoder.parser_step) {
    case STI3353DecoderStepReset:

        if((!level) && (duration >= STI3353_GUARD_MIN) && (duration <= STI3353_GUARD_MAX)) {
            instance->decoder.parser_step = STI3353DecoderStepSaveDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
        }
        break;
    case STI3353DecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = STI3353DecoderStepCheckDuration;
        }
        break;
    case STI3353DecoderStepCheckDuration:
        if(!level) {
            if(duration >= (tl * 2)) {

                instance->decoder.parser_step = STI3353DecoderStepSaveDuration;
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_sti3353_const.min_count_bit_for_found) {

                    if((instance->last_data == instance->decoder.decode_data) &&
                       instance->last_data) {
                        instance->generic.data = instance->decoder.decode_data;
                        instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                        instance->generic.serial =
                            (uint32_t)(instance->decoder.decode_data & 0xFFFFFFFFUL);
                        instance->generic.btn = 0;
                        instance->generic.cnt = 0;
                        if(instance->base.callback)
                            instance->base.callback(&instance->base, instance->base.context);
                    }
                    instance->last_data = instance->decoder.decode_data;
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                break;
            }
            if((DURATION_DIFF(instance->decoder.te_last, ts) < td) &&
               (DURATION_DIFF(duration, tl) < td)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = STI3353DecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, tl) < td) &&
                (DURATION_DIFF(duration, ts) < td)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = STI3353DecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = STI3353DecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = STI3353DecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_sti3353_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderSTI3353* instance = context;
    return subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus
    subghz_protocol_decoder_sti3353_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderSTI3353* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_sti3353_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_sti3353_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderSTI3353* instance = context;
    const SubGhzDecodeInput input = {
        .generic = &instance->generic,
    };
    subghz_decode_plugin_get_string("STI-3353", &input, output);
}
