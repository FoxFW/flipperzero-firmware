#include "mitsubishi_v0.h"
#include <inttypes.h>

#include "../blocks/custom_btn_i.h"
#include "common.h"
#include "../encode_plugin.h"

#define TAG                          "MitsubishiProtocolV0"
#define MITSUBISHI_V0_PREAMBLE_COUNT 100
#define MITSUBISHI_V0_BIT_TE         250
#define MITSUBISHI_V0_BIT_TE_GAP     500
#define MITSUBISHI_V0_BIT_COUNT      96
#define MITSUBISHI_V0_TOTAL_BURSTS   3
#define MITSUBISHI_V0_INTER_BURST_GAP 25000

static const SubGhzBlockConst subghz_protocol_mitsubishi_v0_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 100,
    .min_count_bit_for_found = 80,
};

struct SubGhzProtocolDecoderMitsubishiV0 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint8_t decoder_state;
    uint16_t bit_count;
    uint8_t decode_data[12];
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderMitsubishiV0);

typedef enum {
    MitsubishiV0DecoderStepReset = 0,
    MitsubishiV0DecoderStepDataSave,
    MitsubishiV0DecoderStepDataCheck,
} MitsubishiV0DecoderStep;

struct SubGhzProtocolEncoderMitsubishiV0 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    size_t upload_capacity;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderMitsubishiV0);


static void mitsubishi_v0_reset_payload(SubGhzProtocolDecoderMitsubishiV0* instance) {
    instance->bit_count = 0;
    memset(instance->decode_data, 0, sizeof(instance->decode_data));
}

static bool mitsubishi_v0_collect_pair(
    SubGhzProtocolDecoderMitsubishiV0* instance,
    uint32_t high,
    uint32_t low) {
    bool bit_value;

    if(DURATION_DIFF(high, subghz_protocol_mitsubishi_v0_const.te_short) <
           subghz_protocol_mitsubishi_v0_const.te_delta &&
       DURATION_DIFF(low, subghz_protocol_mitsubishi_v0_const.te_long) <
           subghz_protocol_mitsubishi_v0_const.te_delta) {
        bit_value = true;
    } else if(
        DURATION_DIFF(high, subghz_protocol_mitsubishi_v0_const.te_long) <
            subghz_protocol_mitsubishi_v0_const.te_delta &&
        DURATION_DIFF(low, subghz_protocol_mitsubishi_v0_const.te_short) <
            subghz_protocol_mitsubishi_v0_const.te_delta) {
        bit_value = false;
    } else {
        return false;
    }

    uint16_t bit_index = instance->bit_count;
    if(bit_index < MITSUBISHI_V0_BIT_COUNT) {
        if(bit_value) {
            uint8_t byte_index = bit_index >> 3;
            uint8_t bit_position = 7 - (bit_index & 0x07);
            instance->decode_data[byte_index] |= (1U << bit_position);
        }
        instance->bit_count++;
    }

    return true;
}

static void mitsubishi_v0_publish_frame(SubGhzProtocolDecoderMitsubishiV0* instance) {
    uint8_t payload[12];
    memcpy(payload, instance->decode_data, sizeof(payload));

    for(uint8_t i = 0; i < 8; i++) {
        payload[i] = (uint8_t)~payload[i];
    }

    uint16_t counter = ((uint16_t)payload[4] << 8) | payload[5];
    uint8_t hi = (counter >> 8) & 0xFF;
    uint8_t lo = counter & 0xFF;
    uint8_t mask1 = (hi & 0xAAU) | (lo & 0x55U);
    uint8_t mask2 = (lo & 0xAAU) | (hi & 0x55U);
    uint8_t mask3 = mask1 ^ mask2;

    for(uint8_t i = 0; i < 5; i++) {
        payload[i] ^= mask3;
    }

    instance->generic.data_count_bit = instance->bit_count;
    instance->generic.serial = ((uint32_t)payload[0] << 24) | ((uint32_t)payload[1] << 16) |
                               ((uint32_t)payload[2] << 8) | payload[3];
    instance->generic.cnt = counter;
    instance->generic.btn = payload[6];

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

const SubGhzProtocolDecoder subghz_protocol_mitsubishi_v0_decoder = {
    .alloc = subghz_protocol_decoder_mitsubishi_v0_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_mitsubishi_v0_feed,
    .reset = subghz_protocol_decoder_mitsubishi_v0_reset,
    .get_hash_data = subghz_protocol_decoder_mitsubishi_v0_get_hash_data,
    .serialize = subghz_protocol_decoder_mitsubishi_v0_serialize,
    .deserialize = subghz_protocol_decoder_mitsubishi_v0_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_mitsubishi_v0_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_mitsubishi_v0");

const SubGhzProtocol subghz_protocol_mitsubishi_v0 = {
    .name = MITSUBISHI_PROTOCOL_V0_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 |
            SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_mitsubishi_v0_decoder,
    .encoder = &subghz_protocol_mitsubishi_v0_encoder_ext.vtable,
};

void* subghz_protocol_decoder_mitsubishi_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderMitsubishiV0* instance = calloc(1, sizeof(SubGhzProtocolDecoderMitsubishiV0));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_mitsubishi_v0;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}



void subghz_protocol_decoder_mitsubishi_v0_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderMitsubishiV0* instance = context;
    instance->decoder_state = MitsubishiV0DecoderStepReset;
    instance->decoder.te_last = 0;
    instance->generic.data_count_bit = 0;
    mitsubishi_v0_reset_payload(instance);
}

void subghz_protocol_decoder_mitsubishi_v0_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderMitsubishiV0* instance = context;

    switch(instance->decoder_state) {
    case MitsubishiV0DecoderStepReset:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder_state = MitsubishiV0DecoderStepDataCheck;
        }
        break;

    case MitsubishiV0DecoderStepDataSave:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder_state = MitsubishiV0DecoderStepDataCheck;
        } else {
            instance->decoder_state = MitsubishiV0DecoderStepReset;
            mitsubishi_v0_reset_payload(instance);
        }
        break;

    case MitsubishiV0DecoderStepDataCheck:
        if(!level) {
            if(mitsubishi_v0_collect_pair(instance, instance->decoder.te_last, duration)) {
                if(instance->bit_count >= MITSUBISHI_V0_BIT_COUNT) {
                    mitsubishi_v0_publish_frame(instance);
                    mitsubishi_v0_reset_payload(instance);
                    instance->decoder_state = MitsubishiV0DecoderStepReset;
                } else {
                    instance->decoder_state = MitsubishiV0DecoderStepDataSave;
                }
            } else {
                mitsubishi_v0_reset_payload(instance);
                instance->decoder_state = MitsubishiV0DecoderStepReset;
            }
        } else {
            instance->decoder.te_last = duration;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_mitsubishi_v0_get_hash_data(void* context) {
    SubGhzProtocolDecoderMitsubishiV0* instance = context;
    uint8_t hash = 0;
    for(size_t i = 0; i < 12; i++) {
        hash ^= instance->decode_data[i];
    }
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_mitsubishi_v0_serialize(
    void* context,
    FlipperFormat* ff,
    SubGhzRadioPreset* preset) {
    SubGhzProtocolDecoderMitsubishiV0* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_serialize(&instance->generic, ff, preset);
    if(ret == SubGhzProtocolStatusOk) {
        flipper_format_write_uint32(ff, "Serial", &instance->generic.serial, 1);
        flipper_format_write_uint32(ff, "Cnt", &instance->generic.cnt, 1);
        uint32_t btn = instance->generic.btn;
        flipper_format_write_uint32(ff, "Btn", &btn, 1);
    }
    return ret;
}

SubGhzProtocolStatus subghz_protocol_decoder_mitsubishi_v0_deserialize(void* context, FlipperFormat* ff) {
    SubGhzProtocolDecoderMitsubishiV0* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(&instance->generic, ff, subghz_protocol_mitsubishi_v0_const.min_count_bit_for_found);
}

