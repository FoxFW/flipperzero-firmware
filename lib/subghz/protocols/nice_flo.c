#include "nice_flo.h"

#include "common.h"
#include "../encode_plugin.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolNiceFlo"

static const SubGhzBlockConst subghz_protocol_nice_flo_const = {
    .te_short = 700,
    .te_long = 1400,
    .te_delta = 200,
    .min_count_bit_for_found = 12,
};

struct SubGhzProtocolDecoderNiceFlo {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderNiceFlo {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderNiceFlo);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderNiceFlo);

typedef enum {
    NiceFloDecoderStepReset = 0,
    NiceFloDecoderStepFoundStartBit,
    NiceFloDecoderStepSaveDuration,
    NiceFloDecoderStepCheckDuration,
} NiceFloDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_nice_flo_decoder = {
    .alloc = subghz_protocol_decoder_nice_flo_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_nice_flo_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_nice_flo_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_nice_flo_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_nice_flo");

const SubGhzProtocol subghz_protocol_nice_flo = {
    .name = SUBGHZ_PROTOCOL_NICE_FLO_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_nice_flo_decoder,
    .encoder = &subghz_protocol_nice_flo_encoder_ext.vtable,
};

void* subghz_protocol_decoder_nice_flo_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderNiceFlo), &subghz_protocol_nice_flo);
}

void subghz_protocol_decoder_nice_flo_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderNiceFlo* instance = context;

    switch(instance->decoder.parser_step) {
    case NiceFloDecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_nice_flo_const.te_short * 36) <
                        subghz_protocol_nice_flo_const.te_delta * 36)) {

            instance->decoder.parser_step = NiceFloDecoderStepFoundStartBit;
        }
        break;
    case NiceFloDecoderStepFoundStartBit:
        if(!level) {
            break;
        } else if(
            DURATION_DIFF(duration, subghz_protocol_nice_flo_const.te_short) <
            subghz_protocol_nice_flo_const.te_delta) {

            instance->decoder.parser_step = NiceFloDecoderStepSaveDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
        } else {
            instance->decoder.parser_step = NiceFloDecoderStepReset;
        }
        break;
    case NiceFloDecoderStepSaveDuration:
        if(!level) {
            if(duration >= (subghz_protocol_nice_flo_const.te_short * 4)) {
                instance->decoder.parser_step = NiceFloDecoderStepFoundStartBit;
                if(instance->decoder.decode_count_bit >=
                   subghz_protocol_nice_flo_const.min_count_bit_for_found) {
                    instance->generic.serial = 0x0;
                    instance->generic.btn = 0x0;

                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                break;
            }
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = NiceFloDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = NiceFloDecoderStepReset;
        }
        break;
    case NiceFloDecoderStepCheckDuration:
        if(level) {
            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_nice_flo_const.te_short) <
                subghz_protocol_nice_flo_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_nice_flo_const.te_long) <
                subghz_protocol_nice_flo_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = NiceFloDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_nice_flo_const.te_long) <
                 subghz_protocol_nice_flo_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_nice_flo_const.te_short) <
                 subghz_protocol_nice_flo_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = NiceFloDecoderStepSaveDuration;
            } else
                instance->decoder.parser_step = NiceFloDecoderStepReset;
        } else {
            instance->decoder.parser_step = NiceFloDecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_nice_flo_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderNiceFlo* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }
        if((instance->generic.data_count_bit <
            subghz_protocol_nice_flo_const.min_count_bit_for_found) ||
           (instance->generic.data_count_bit >
            2 * subghz_protocol_nice_flo_const.min_count_bit_for_found)) {
            FURI_LOG_E(TAG, "Wrong number of bits in key");
            ret = SubGhzProtocolStatusErrorValueBitCount;
            break;
        }
    } while(false);
    return ret;
}

