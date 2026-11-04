#include "power_smart.h"

#include "common.h"
#include "../encode_plugin.h"
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolPowerSmart"

#define POWER_SMART_PACKET_HEADER      0xFD000000AA000000
#define POWER_SMART_PACKET_HEADER_MASK 0xFF000000FF000000

#define CHANNEL_PATTERN "%c%c%c%c%c%c"
#define CNT_TO_CHANNEL(dip)                                                             \
    (dip & 0x0001 ? '*' : '-'), (dip & 0x0002 ? '*' : '-'), (dip & 0x0004 ? '*' : '-'), \
        (dip & 0x0008 ? '*' : '-'), (dip & 0x0010 ? '*' : '-'), (dip & 0x0020 ? '*' : '-')

static const SubGhzBlockConst subghz_protocol_power_smart_const = {
    .te_short = 225,
    .te_long = 450,
    .te_delta = 100,
    .min_count_bit_for_found = 64,
};

struct SubGhzProtocolDecoderPowerSmart {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    ManchesterState manchester_saved_state;
    uint16_t header_count;
};

struct SubGhzProtocolEncoderPowerSmart {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderPowerSmart);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderPowerSmart);

typedef enum {
    PowerSmartDecoderStepReset = 0,
    PowerSmartDecoderFoundHeader,
    PowerSmartDecoderStepDecoderData,
} PowerSmartDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_power_smart_decoder = {
    .alloc = subghz_protocol_decoder_power_smart_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_power_smart_feed,
    .reset = subghz_protocol_decoder_power_smart_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_power_smart_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_power_smart_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_power_smart");

const SubGhzProtocol subghz_protocol_power_smart = {
    .name = SUBGHZ_PROTOCOL_POWER_SMART_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_power_smart_decoder,
    .encoder = &subghz_protocol_power_smart_encoder_ext.vtable,
};

void* subghz_protocol_decoder_power_smart_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderPowerSmart), &subghz_protocol_power_smart);
}

void subghz_protocol_decoder_power_smart_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderPowerSmart* instance = context;
    manchester_advance(
        instance->manchester_saved_state,
        ManchesterEventReset,
        &instance->manchester_saved_state,
        NULL);
}

bool subghz_protocol_power_smart_check_valid(uint64_t packet) {
    uint32_t data_1 = (uint32_t)((packet >> 40) & 0xFFFF);
    uint32_t data_2 = (uint32_t)((~packet >> 8) & 0xFFFF);
    uint8_t data_3 = (uint8_t)(packet >> 32) & 0xFF;
    uint8_t data_4 = (uint8_t)(((~packet) & 0xFF) - 1);
    return (data_1 == data_2) && (data_3 == data_4);
}

void subghz_protocol_decoder_power_smart_feed(
    void* context,
    bool level,
    volatile uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderPowerSmart* instance = context;
    ManchesterEvent event = ManchesterEventReset;
    if(!level) {
        if(DURATION_DIFF(duration, subghz_protocol_power_smart_const.te_short) <
           subghz_protocol_power_smart_const.te_delta) {
            event = ManchesterEventShortLow;
        } else if(
            DURATION_DIFF(duration, subghz_protocol_power_smart_const.te_long) <
            subghz_protocol_power_smart_const.te_delta * 2) {
            event = ManchesterEventLongLow;
        }
    } else {
        if(DURATION_DIFF(duration, subghz_protocol_power_smart_const.te_short) <
           subghz_protocol_power_smart_const.te_delta) {
            event = ManchesterEventShortHigh;
        } else if(
            DURATION_DIFF(duration, subghz_protocol_power_smart_const.te_long) <
            subghz_protocol_power_smart_const.te_delta * 2) {
            event = ManchesterEventLongHigh;
        }
    }
    if(event != ManchesterEventReset) {
        bool data;
        bool data_ok = manchester_advance(
            instance->manchester_saved_state, event, &instance->manchester_saved_state, &data);

        if(data_ok) {
            instance->decoder.decode_data = (instance->decoder.decode_data << 1) | !data;
        }
        if((instance->decoder.decode_data & POWER_SMART_PACKET_HEADER_MASK) ==
           POWER_SMART_PACKET_HEADER) {
            if(subghz_protocol_power_smart_check_valid(instance->decoder.decode_data)) {
                instance->generic.data = instance->decoder.decode_data;
                instance->generic.data_count_bit =
                    subghz_protocol_power_smart_const.min_count_bit_for_found;
                if(instance->base.callback)
                    instance->base.callback(&instance->base, instance->base.context);
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
            }
        }
    } else {
        instance->decoder.decode_data = 0;
        instance->decoder.decode_count_bit = 0;
        manchester_advance(
            instance->manchester_saved_state,
            ManchesterEventReset,
            &instance->manchester_saved_state,
            NULL);
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_power_smart_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderPowerSmart* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_power_smart_const.min_count_bit_for_found);
}

