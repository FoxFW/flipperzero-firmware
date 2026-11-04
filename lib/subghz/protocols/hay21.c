#include "hay21.h"

#include "common.h"
#include "../encode_plugin.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#include "../blocks/custom_btn_i.h"

#define TAG "SubGhzProtocolHay21"

static bool hay21_raw_replay = false; // #16: TxRaw verbatim replay (freeze counter)

static const SubGhzBlockConst subghz_protocol_hay21_const = {
    .te_short = 300,
    .te_long = 700,
    .te_delta = 150,
    .min_count_bit_for_found = 21,
};

struct SubGhzProtocolDecoderHay21 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderHay21 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderHay21);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderHay21);

typedef enum {
    Hay21DecoderStepReset = 0,
    Hay21DecoderStepSaveDuration,
    Hay21DecoderStepCheckDuration,
} Hay21DecoderStep;

const SubGhzProtocolDecoder subghz_protocol_hay21_decoder = {
    .alloc = subghz_protocol_decoder_hay21_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_hay21_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_hay21_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

/* host bridge: firmware functions/state the SD encoder uses (handed over at load) */
typedef struct {
    void* hay21_raw_replay;
} enc_hay21_host_t;
static const enc_hay21_host_t enc_hay21_host = {
    .hay21_raw_replay = (void*)&hay21_raw_replay,
};
static const SubGhzEncoderExternal subghz_protocol_hay21_encoder_ext = SUBGHZ_ENCODER_EXTERNAL_HOST("enc_hay21", &enc_hay21_host);

const SubGhzProtocol subghz_protocol_hay21 = {
    .name = SUBGHZ_PROTOCOL_HAY21_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,

    .decoder = &subghz_protocol_hay21_decoder,
    .encoder = &subghz_protocol_hay21_encoder_ext.vtable,
};

void* subghz_protocol_decoder_hay21_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderHay21), &subghz_protocol_hay21);
}

void subghz_protocol_decoder_hay21_feed(void* context, bool level, volatile uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderHay21* instance = context;

    switch(instance->decoder.parser_step) {
    case Hay21DecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_hay21_const.te_long * 6) <
                        subghz_protocol_hay21_const.te_delta * 3)) {

            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = Hay21DecoderStepSaveDuration;
        }
        break;
    case Hay21DecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = Hay21DecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = Hay21DecoderStepReset;
        }
        break;
    case Hay21DecoderStepCheckDuration:
        if(!level) {

            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_hay21_const.te_long) <
                subghz_protocol_hay21_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_hay21_const.te_short) <
                subghz_protocol_hay21_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = Hay21DecoderStepSaveDuration;

            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_hay21_const.te_short) <
                 subghz_protocol_hay21_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_hay21_const.te_long) <
                 subghz_protocol_hay21_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = Hay21DecoderStepSaveDuration;
            } else if(

                DURATION_DIFF(duration, subghz_protocol_hay21_const.te_long * 6) <
                subghz_protocol_hay21_const.te_delta * 2) {

                if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_hay21_const.te_long) <
                    subghz_protocol_hay21_const.te_delta)) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                }
                if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_hay21_const.te_short) <
                    subghz_protocol_hay21_const.te_delta)) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                }

                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_hay21_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->decoder.parser_step = Hay21DecoderStepReset;
            } else {
                instance->decoder.parser_step = Hay21DecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = Hay21DecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_hay21_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderHay21* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_hay21_const.min_count_bit_for_found);
}

