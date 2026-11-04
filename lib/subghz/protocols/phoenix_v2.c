#include "phoenix_v2.h"

#include "common.h"
#include "../encode_plugin.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#include "../blocks/custom_btn_i.h"

#define TAG "SubGhzProtocolPhoenixV2"

static bool phoenix_v2_raw_replay = false; // #16: TxRaw verbatim replay (freeze counter)

static bool bypass = false;
static uint8_t v2_phoenix_counter_mode = 0;

static const SubGhzBlockConst subghz_protocol_phoenix_v2_const = {
    .te_short = 427,
    .te_long = 853,
    .te_delta = 100,
    .min_count_bit_for_found = 52,
};

struct SubGhzProtocolDecoderPhoenix_V2 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderPhoenix_V2 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderPhoenix_V2);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderPhoenix_V2);

typedef enum {
    Phoenix_V2DecoderStepReset = 0,
    Phoenix_V2DecoderStepFoundStartBit,
    Phoenix_V2DecoderStepSaveDuration,
    Phoenix_V2DecoderStepCheckDuration,
} Phoenix_V2DecoderStep;

const SubGhzProtocolDecoder subghz_protocol_phoenix_v2_decoder = {
    .alloc = subghz_protocol_decoder_phoenix_v2_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_phoenix_v2_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_phoenix_v2_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

/* host bridge: firmware functions/state the SD encoder uses (handed over at load) */
typedef struct {
    void* bypass;
    void* phoenix_v2_raw_replay;
    void* v2_phoenix_counter_mode;
} enc_phoenix_v2_host_t;
static const enc_phoenix_v2_host_t enc_phoenix_v2_host = {
    .bypass = (void*)&bypass,
    .phoenix_v2_raw_replay = (void*)&phoenix_v2_raw_replay,
    .v2_phoenix_counter_mode = (void*)&v2_phoenix_counter_mode,
};
static const SubGhzEncoderExternal subghz_protocol_phoenix_v2_encoder_ext = SUBGHZ_ENCODER_EXTERNAL_HOST("enc_phoenix_v2", &enc_phoenix_v2_host);

const SubGhzProtocol subghz_protocol_phoenix_v2 = {
    .name = SUBGHZ_PROTOCOL_PHOENIX_V2_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,

    .decoder = &subghz_protocol_phoenix_v2_decoder,
    .encoder = &subghz_protocol_phoenix_v2_encoder_ext.vtable,
};

static uint16_t subghz_protocol_phoenix_v2_encrypt_counter(uint64_t full_key, uint16_t counter);

bool subghz_protocol_phoenix_v2_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint16_t cnt,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolEncoderPhoenix_V2* instance = context;
    instance->generic.btn = 0x1;
    instance->generic.serial = serial;
    instance->generic.cnt = cnt;
    instance->generic.data_count_bit = 52;

    uint64_t local_data_rev =
        (uint64_t)(((uint64_t)instance->generic.cnt << 40) |
                   ((uint64_t)instance->generic.btn << 32) | (uint64_t)instance->generic.serial);

    uint16_t encrypted_counter = (uint16_t)subghz_protocol_phoenix_v2_encrypt_counter(
        local_data_rev, instance->generic.cnt);

    instance->generic.data = subghz_protocol_blocks_reverse_key(
                                 (uint64_t)(((uint64_t)encrypted_counter << 40) |
                                            ((uint64_t)instance->generic.btn << 32) |
                                            (uint64_t)instance->generic.serial),
                                 instance->generic.data_count_bit + 4) &
                             0xFFFFFFFFFFFFF;

    return SubGhzProtocolStatusOk ==
           subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

void* subghz_protocol_decoder_phoenix_v2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderPhoenix_V2), &subghz_protocol_phoenix_v2);
}

void subghz_protocol_decoder_phoenix_v2_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderPhoenix_V2* instance = context;

    switch(instance->decoder.parser_step) {
    case Phoenix_V2DecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_phoenix_v2_const.te_short * 60) <
                        subghz_protocol_phoenix_v2_const.te_delta * 30)) {

            instance->decoder.parser_step = Phoenix_V2DecoderStepFoundStartBit;
        }
        break;
    case Phoenix_V2DecoderStepFoundStartBit:
        if(level && (DURATION_DIFF(duration, (subghz_protocol_phoenix_v2_const.te_short * 6)) <
                     subghz_protocol_phoenix_v2_const.te_delta * 4)) {

            instance->decoder.parser_step = Phoenix_V2DecoderStepSaveDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
        } else {
            instance->decoder.parser_step = Phoenix_V2DecoderStepReset;
        }
        break;
    case Phoenix_V2DecoderStepSaveDuration:
        if(!level) {
            if(duration >= ((uint32_t)subghz_protocol_phoenix_v2_const.te_short * 10 +
                            subghz_protocol_phoenix_v2_const.te_delta)) {
                instance->decoder.parser_step = Phoenix_V2DecoderStepFoundStartBit;
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_phoenix_v2_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                break;
            } else {
                instance->decoder.te_last = duration;
                instance->decoder.parser_step = Phoenix_V2DecoderStepCheckDuration;
            }
        }
        break;
    case Phoenix_V2DecoderStepCheckDuration:
        if(level) {
            if((DURATION_DIFF(
                    instance->decoder.te_last, subghz_protocol_phoenix_v2_const.te_short) <
                subghz_protocol_phoenix_v2_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_phoenix_v2_const.te_long) <
                subghz_protocol_phoenix_v2_const.te_delta * 3)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = Phoenix_V2DecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(
                     instance->decoder.te_last, subghz_protocol_phoenix_v2_const.te_long) <
                 subghz_protocol_phoenix_v2_const.te_delta * 3) &&
                (DURATION_DIFF(duration, subghz_protocol_phoenix_v2_const.te_short) <
                 subghz_protocol_phoenix_v2_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = Phoenix_V2DecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = Phoenix_V2DecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = Phoenix_V2DecoderStepReset;
        }
        break;
    }
}

static uint16_t subghz_protocol_phoenix_v2_encrypt_counter(uint64_t full_key, uint16_t counter) {
    uint8_t xor_key1 = (uint8_t)(full_key >> 24);
    uint8_t xor_key2 = (uint8_t)((full_key >> 16) & 0xFF);

    uint8_t byte2 = (uint8_t)(counter >> 8);
    uint8_t byte1 = (uint8_t)(counter & 0xFF);

    for(int i = 0; i < 16; i++) {

        uint8_t msb_of_prev_byte1 = byte2 & 0x80;

        if(msb_of_prev_byte1 == 0) {

            byte2 ^= xor_key2;
            byte1 ^= xor_key1;
        }

        uint8_t lsb_of_current_byte1 = byte1 & 1;

        byte2 = (byte2 << 1) | lsb_of_current_byte1;
        byte1 = (byte1 >> 1) | msb_of_prev_byte1;
    }

    return (uint16_t)byte1 << 8 | byte2;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_phoenix_v2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderPhoenix_V2* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_phoenix_v2_const.min_count_bit_for_found);

    if(!flipper_format_rewind(flipper_format)) {
        FURI_LOG_E(TAG, "Rewind error");
        return SubGhzProtocolStatusError;
    }

    uint32_t tmp_counter_mode;
    if(flipper_format_read_uint32(flipper_format, "CounterMode", &tmp_counter_mode, 1)) {
        v2_phoenix_counter_mode = (uint8_t)tmp_counter_mode;
    } else {
        v2_phoenix_counter_mode = 0;
    }
    return ret;
}

