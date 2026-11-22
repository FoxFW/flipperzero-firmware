#include "doorbell32.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "common.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"

#define TAG "SubGhzProtocolDoorbell32"

#define DOORBELL32_GUARD_TIME 14u

static const SubGhzBlockConst subghz_protocol_doorbell32_const = {
    .te_short = 515,
    .te_long = 1545,
    .te_delta = 200,
    .min_count_bit_for_found = 32,
};

struct SubGhzProtocolDecoderDoorbell32 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t te;
    uint32_t last_data;
};
SUBGHZ_ASSERT_DECODER_TE_LAYOUT(SubGhzProtocolDecoderDoorbell32);

struct SubGhzProtocolEncoderDoorbell32 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint32_t te;
};
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderDoorbell32);

typedef enum {
    Doorbell32DecoderStepReset = 0,
    Doorbell32DecoderStepSaveDuration,
    Doorbell32DecoderStepCheckDuration,
} Doorbell32DecoderStep;

const SubGhzProtocolDecoder subghz_protocol_doorbell32_decoder = {
    .alloc = subghz_protocol_decoder_doorbell32_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_doorbell32_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_doorbell32_serialize,
    .deserialize = subghz_protocol_decoder_doorbell32_deserialize,
    .get_string = subghz_protocol_decoder_doorbell32_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_doorbell32_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_doorbell32");

const SubGhzProtocol subghz_protocol_doorbell32 = {
    .name = SUBGHZ_PROTOCOL_DOORBELL32_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send |
            SubGhzProtocolFlag_Sensors,

    .decoder = &subghz_protocol_doorbell32_decoder,
    .encoder = &subghz_protocol_doorbell32_encoder_ext.vtable,
};

void* subghz_protocol_decoder_doorbell32_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderDoorbell32), &subghz_protocol_doorbell32);
}

void subghz_protocol_decoder_doorbell32_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderDoorbell32* instance = context;
    const SubGhzBlockConst* c = &subghz_protocol_doorbell32_const;

    switch(instance->decoder.parser_step) {
    case Doorbell32DecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, c->te_short * DOORBELL32_GUARD_TIME) <
                        c->te_delta * DOORBELL32_GUARD_TIME / 2)) {
            instance->decoder.parser_step = Doorbell32DecoderStepSaveDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->te = 0;
        }
        break;
    case Doorbell32DecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->te += duration;
            instance->decoder.parser_step = Doorbell32DecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = Doorbell32DecoderStepReset;
        }
        break;
    case Doorbell32DecoderStepCheckDuration:
        if(!level) {
            if(duration >= ((uint32_t)c->te_long * 2)) {

                if((instance->decoder.decode_count_bit == c->min_count_bit_for_found) &&
                   (instance->last_data == instance->decoder.decode_data) && instance->last_data) {
                    instance->te /= (instance->decoder.decode_count_bit * 4 + 1);
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->last_data = instance->decoder.decode_data;
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->te = 0;
                instance->decoder.parser_step = Doorbell32DecoderStepSaveDuration;
                break;
            }
            instance->te += duration;

            if((DURATION_DIFF(instance->decoder.te_last, c->te_short) < c->te_delta) &&
               (DURATION_DIFF(duration, c->te_long) < c->te_delta * 2)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = Doorbell32DecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, c->te_long) < c->te_delta * 2) &&
                (DURATION_DIFF(duration, c->te_short) < c->te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = Doorbell32DecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = Doorbell32DecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = Doorbell32DecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_doorbell32_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    return subghz_protocol_decoder_common_serialize_te(context, flipper_format, preset);
}

SubGhzProtocolStatus
    subghz_protocol_decoder_doorbell32_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderDoorbell32* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_doorbell32_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_doorbell32_get_string(void* context, FuriString* output) {
    SubGhzProtocolDecoderDoorbell32* instance = context;
    const SubGhzDecodeInput input = {
        .decoder = context,
        .generic = &instance->generic,
        .extra = NULL,
        .extra_count = 0,
        .aux = NULL,
    };
    subghz_decode_plugin_get_string(instance->generic.protocol_name, &input, output);
}

