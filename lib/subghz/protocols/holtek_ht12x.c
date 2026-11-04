#include "holtek_ht12x.h"

#include "common.h"
#include "../encode_plugin.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolHoltekHt12x"

#define DIP_PATTERN "%c%c%c%c%c%c%c%c"
#define CNT_TO_DIP(dip)                                                                     \
    (dip & 0x0080 ? '0' : '1'), (dip & 0x0040 ? '0' : '1'), (dip & 0x0020 ? '0' : '1'),     \
        (dip & 0x0010 ? '0' : '1'), (dip & 0x0008 ? '0' : '1'), (dip & 0x0004 ? '0' : '1'), \
        (dip & 0x0002 ? '0' : '1'), (dip & 0x0001 ? '0' : '1')

static const SubGhzBlockConst subghz_protocol_holtek_th12x_const = {
    .te_short = 320,
    .te_long = 640,
    .te_delta = 200,
    .min_count_bit_for_found = 12,
};

struct SubGhzProtocolDecoderHoltek_HT12X {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t te;
    uint32_t last_data;
};

struct SubGhzProtocolEncoderHoltek_HT12X {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint32_t te;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderHoltek_HT12X);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderHoltek_HT12X);

typedef enum {
    Holtek_HT12XDecoderStepReset = 0,
    Holtek_HT12XDecoderStepFoundStartBit,
    Holtek_HT12XDecoderStepSaveDuration,
    Holtek_HT12XDecoderStepCheckDuration,
} Holtek_HT12XDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_holtek_th12x_decoder = {
    .alloc = subghz_protocol_decoder_holtek_th12x_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_holtek_th12x_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_holtek_th12x_serialize,
    .deserialize = subghz_protocol_decoder_holtek_th12x_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_holtek_th12x_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_holtek_ht12x");

const SubGhzProtocol subghz_protocol_holtek_th12x = {
    .name = SUBGHZ_PROTOCOL_HOLTEK_HT12X_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 | SubGhzProtocolFlag_315 |
            SubGhzProtocolFlag_AM | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_holtek_th12x_decoder,
    .encoder = &subghz_protocol_holtek_th12x_encoder_ext.vtable,
};

void* subghz_protocol_decoder_holtek_th12x_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderHoltek_HT12X), &subghz_protocol_holtek_th12x);
}

void subghz_protocol_decoder_holtek_th12x_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderHoltek_HT12X* instance = context;

    switch(instance->decoder.parser_step) {
    case Holtek_HT12XDecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_holtek_th12x_const.te_short * 28) <
                        subghz_protocol_holtek_th12x_const.te_delta * 20)) {

            instance->decoder.parser_step = Holtek_HT12XDecoderStepFoundStartBit;
        }
        break;
    case Holtek_HT12XDecoderStepFoundStartBit:
        if((level) && (DURATION_DIFF(duration, subghz_protocol_holtek_th12x_const.te_short) <
                       subghz_protocol_holtek_th12x_const.te_delta)) {

            instance->decoder.parser_step = Holtek_HT12XDecoderStepSaveDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->te = duration;
        } else {
            instance->decoder.parser_step = Holtek_HT12XDecoderStepReset;
        }
        break;
    case Holtek_HT12XDecoderStepSaveDuration:

        if(!level) {
            if(duration >= ((uint32_t)subghz_protocol_holtek_th12x_const.te_short * 10 +
                            subghz_protocol_holtek_th12x_const.te_delta)) {
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_holtek_th12x_const.min_count_bit_for_found) {
                    if((instance->last_data == instance->decoder.decode_data) &&
                       instance->last_data) {
                        instance->te /= (instance->decoder.decode_count_bit * 3 + 1);

                        instance->generic.data = instance->decoder.decode_data;
                        instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                        if(instance->base.callback)
                            instance->base.callback(&instance->base, instance->base.context);
                    }
                    instance->last_data = instance->decoder.decode_data;
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->te = 0;
                instance->decoder.parser_step = Holtek_HT12XDecoderStepFoundStartBit;
                break;
            } else {
                instance->decoder.te_last = duration;
                instance->te += duration;
                instance->decoder.parser_step = Holtek_HT12XDecoderStepCheckDuration;
            }
        } else {
            instance->decoder.parser_step = Holtek_HT12XDecoderStepReset;
        }
        break;
    case Holtek_HT12XDecoderStepCheckDuration:
        if(level) {
            instance->te += duration;
            if((DURATION_DIFF(
                    instance->decoder.te_last, subghz_protocol_holtek_th12x_const.te_long) <
                subghz_protocol_holtek_th12x_const.te_delta * 2) &&
               (DURATION_DIFF(duration, subghz_protocol_holtek_th12x_const.te_short) <
                subghz_protocol_holtek_th12x_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = Holtek_HT12XDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(
                     instance->decoder.te_last, subghz_protocol_holtek_th12x_const.te_short) <
                 subghz_protocol_holtek_th12x_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_holtek_th12x_const.te_long) <
                 subghz_protocol_holtek_th12x_const.te_delta * 2)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = Holtek_HT12XDecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = Holtek_HT12XDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = Holtek_HT12XDecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_holtek_th12x_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderHoltek_HT12X* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if((ret == SubGhzProtocolStatusOk) &&
       !flipper_format_write_uint32(flipper_format, "TE", &instance->te, 1)) {
        FURI_LOG_E(TAG, "Unable to add TE");
        ret = SubGhzProtocolStatusErrorParserTe;
    }
    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_holtek_th12x_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderHoltek_HT12X* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_holtek_th12x_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        if(!flipper_format_read_uint32(flipper_format, "TE", (uint32_t*)&instance->te, 1)) {
            FURI_LOG_E(TAG, "Missing TE");
            ret = SubGhzProtocolStatusErrorParserTe;
            break;
        }
    } while(false);
    return ret;
}

