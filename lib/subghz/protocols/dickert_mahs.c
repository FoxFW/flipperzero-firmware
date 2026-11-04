#include "dickert_mahs.h"

#include "common.h"
#include "../encode_plugin.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_rtc.h>

#define TAG "SubGhzProtocolDicketMAHS"

static const SubGhzBlockConst subghz_protocol_dickert_mahs_const = {
    .te_short = 400,
    .te_long = 800,
    .te_delta = 100,
    .min_count_bit_for_found = 36,
};

struct SubGhzProtocolDecoderDickertMAHS {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t tmp[2];
    uint8_t tmp_cnt;
};

struct SubGhzProtocolEncoderDickertMAHS {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderDickertMAHS);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderDickertMAHS);

typedef enum {
    DickertMAHSDecoderStepReset = 0,
    DickertMAHSDecoderStepInitial,
    DickertMAHSDecoderStepRecording,
} DickertMAHSDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_dickert_mahs_decoder = {
    .alloc = subghz_protocol_decoder_dickert_mahs_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_dickert_mahs_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_dickert_mahs_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_dickert_mahs_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_dickert_mahs");

const SubGhzProtocol subghz_protocol_dickert_mahs = {
    .name = SUBGHZ_PROTOCOL_DICKERT_MAHS_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_dickert_mahs_decoder,
    .encoder = &subghz_protocol_dickert_mahs_encoder_ext.vtable,
};

void* subghz_protocol_decoder_dickert_mahs_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderDickertMAHS* instance = malloc(sizeof(SubGhzProtocolDecoderDickertMAHS));
    instance->base.protocol = &subghz_protocol_dickert_mahs;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->tmp_cnt = 0;

    return instance;
}

void subghz_protocol_decoder_dickert_mahs_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderDickertMAHS* instance = context;

    switch(instance->decoder.parser_step) {
    case DickertMAHSDecoderStepReset:

        if(instance->decoder.decode_count_bit >=
           subghz_protocol_dickert_mahs_const.min_count_bit_for_found) {
            instance->generic.serial = 0x0;
            instance->generic.btn = 0x0;

            instance->generic.data = instance->decoder.decode_data;
            instance->generic.data_count_bit = instance->decoder.decode_count_bit;

            if(instance->base.callback)
                instance->base.callback(&instance->base, instance->base.context);

            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
        }

        if((!level) && (DURATION_DIFF(duration, subghz_protocol_dickert_mahs_const.te_long * 50) <
                        subghz_protocol_dickert_mahs_const.te_delta * 70)) {

            instance->decoder.parser_step = DickertMAHSDecoderStepInitial;
        }
        break;
    case DickertMAHSDecoderStepInitial:
        if(!level) {
            break;
        } else if(
            DURATION_DIFF(duration, subghz_protocol_dickert_mahs_const.te_short) <
            subghz_protocol_dickert_mahs_const.te_delta) {

            instance->decoder.parser_step = DickertMAHSDecoderStepRecording;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
        } else {
            instance->decoder.parser_step = DickertMAHSDecoderStepReset;
        }
        break;
    case DickertMAHSDecoderStepRecording:
        if((!level && instance->tmp_cnt == 0) || (level && instance->tmp_cnt == 1)) {
            instance->tmp[instance->tmp_cnt] = duration;

            instance->tmp_cnt++;

            if(instance->tmp_cnt == 2) {
                if(DURATION_DIFF(instance->tmp[0] + instance->tmp[1], 1200) <
                   subghz_protocol_dickert_mahs_const.te_delta) {
                    if(DURATION_DIFF(instance->tmp[0], subghz_protocol_dickert_mahs_const.te_long) <
                       subghz_protocol_dickert_mahs_const.te_delta) {
                        subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                    } else if(
                        DURATION_DIFF(
                            instance->tmp[0], subghz_protocol_dickert_mahs_const.te_short) <
                        subghz_protocol_dickert_mahs_const.te_delta) {
                        subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                    }

                    instance->tmp_cnt = 0;
                } else {
                    instance->tmp_cnt = 0;
                    instance->decoder.parser_step = DickertMAHSDecoderStepReset;
                }
            }
        } else {
            instance->tmp_cnt = 0;
            instance->decoder.parser_step = DickertMAHSDecoderStepReset;
        }

        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_dickert_mahs_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderDickertMAHS* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }

        if((instance->generic.data_count_bit <
            subghz_protocol_dickert_mahs_const.min_count_bit_for_found)) {
            FURI_LOG_E(TAG, "Wrong number of bits in key");
            ret = SubGhzProtocolStatusErrorValueBitCount;
            break;
        }
    } while(false);
    return ret;
}

