#include "faac_tm.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "common.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"

#define TAG "SubGhzProtocolFaacTm"

#define FAAC_TM_HEADER_TE 35
#define FAAC_TM_TE_MIN    100
#define FAAC_TM_TE_MAX    1000

#define DIP_PATTERN "%c%c%c%c%c%c%c%c%c%c%c%c"
#define DATA_TO_DIP(dip)                                                                 \
    (dip & 0x800 ? '1' : '0'), (dip & 0x400 ? '1' : '0'), (dip & 0x200 ? '1' : '0'),     \
        (dip & 0x100 ? '1' : '0'), (dip & 0x080 ? '1' : '0'), (dip & 0x040 ? '1' : '0'), \
        (dip & 0x020 ? '1' : '0'), (dip & 0x010 ? '1' : '0'), (dip & 0x008 ? '1' : '0'), \
        (dip & 0x004 ? '1' : '0'), (dip & 0x002 ? '1' : '0'), (dip & 0x001 ? '1' : '0')

static const SubGhzBlockConst subghz_protocol_faac_tm_const = {
    .te_short = 400,
    .te_long = 800,
    .te_delta = 120,
    .min_count_bit_for_found = 12,
};

struct SubGhzProtocolDecoderFaacTm {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t te;
};
SUBGHZ_ASSERT_DECODER_TE_LAYOUT(SubGhzProtocolDecoderFaacTm);

struct SubGhzProtocolEncoderFaacTm {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint32_t te;
};
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderFaacTm);

typedef enum {
    FaacTmDecoderStepReset = 0,
    FaacTmDecoderStepFoundStartBit,
    FaacTmDecoderStepSaveDuration,
    FaacTmDecoderStepCheckDuration,
} FaacTmDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_faac_tm_decoder = {
    .alloc = subghz_protocol_decoder_faac_tm_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_faac_tm_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize_te,
    .deserialize = subghz_protocol_decoder_faac_tm_deserialize,
    .get_string = subghz_protocol_decoder_faac_tm_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_faac_tm_encoder_ext =
    SUBGHZ_ENCODER_EXTERNAL("enc_faac_tm");

const SubGhzProtocol subghz_protocol_faac_tm = {
    .name = SUBGHZ_PROTOCOL_FAAC_TM_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_faac_tm_decoder,
    .encoder = &subghz_protocol_faac_tm_encoder_ext.vtable,
};

void* subghz_protocol_decoder_faac_tm_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderFaacTm), &subghz_protocol_faac_tm);
}

void subghz_protocol_decoder_faac_tm_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderFaacTm* instance = context;

    switch(instance->decoder.parser_step) {
    case FaacTmDecoderStepReset:
        if((!level) &&
           (DURATION_DIFF(duration, subghz_protocol_faac_tm_const.te_short * FAAC_TM_HEADER_TE) <
            subghz_protocol_faac_tm_const.te_delta * FAAC_TM_HEADER_TE)) {

            instance->decoder.parser_step = FaacTmDecoderStepFoundStartBit;
        }
        break;
    case FaacTmDecoderStepFoundStartBit:
        if(!level) {
            break;
        } else if(
            DURATION_DIFF(duration, subghz_protocol_faac_tm_const.te_short) <
            subghz_protocol_faac_tm_const.te_delta) {

            instance->decoder.parser_step = FaacTmDecoderStepSaveDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->te = duration;
        } else {
            instance->decoder.parser_step = FaacTmDecoderStepReset;
        }
        break;
    case FaacTmDecoderStepSaveDuration:
        if(!level) {
            if(duration >= (subghz_protocol_faac_tm_const.te_short * 4)) {
                instance->decoder.parser_step = FaacTmDecoderStepFoundStartBit;
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_faac_tm_const.min_count_bit_for_found) {

                    instance->te /= (instance->decoder.decode_count_bit * 3 + 1);

                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                break;
            }
            instance->decoder.te_last = duration;
            instance->te += duration;
            instance->decoder.parser_step = FaacTmDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = FaacTmDecoderStepReset;
        }
        break;
    case FaacTmDecoderStepCheckDuration:
        if(level) {
            instance->te += duration;
            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_faac_tm_const.te_short) <
                subghz_protocol_faac_tm_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_faac_tm_const.te_long) <
                subghz_protocol_faac_tm_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = FaacTmDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_faac_tm_const.te_long) <
                 subghz_protocol_faac_tm_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_faac_tm_const.te_short) <
                 subghz_protocol_faac_tm_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = FaacTmDecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = FaacTmDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = FaacTmDecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_faac_tm_deserialize(void* context, FlipperFormat* flipper_format) {
    return subghz_protocol_decoder_common_deserialize_te(
        context, flipper_format, subghz_protocol_faac_tm_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_faac_tm_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderFaacTm* instance = context;
    uint64_t extra[] = {(uint64_t)instance->te};
    const SubGhzDecodeInput input = {
        .generic = &instance->generic,
        .extra = extra,
        .extra_count = 1,
    };
    subghz_decode_plugin_get_string("Faac TM", &input, output);
}
