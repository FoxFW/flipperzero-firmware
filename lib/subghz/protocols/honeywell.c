#include "honeywell.h"

#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "common.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"

#define TAG "SubGhzProtocolHoneywell"

static const SubGhzBlockConst subghz_protocol_honeywell_const = {
    .te_long = 280,
    .te_short = 143,
    .te_delta = 51,
    .min_count_bit_for_found = 64,
};

struct SubGhzProtocolDecoderHoneywell {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockGeneric generic;
    SubGhzBlockDecoder decoder;
    ManchesterState manchester_saved_state;
};

struct SubGhzProtocolEncoderHoneywell {
    SubGhzProtocolEncoderBase base;
    SubGhzBlockGeneric generic;
    SubGhzProtocolBlockEncoder encoder;
};

const SubGhzProtocolDecoder subghz_protocol_honeywell_decoder = {
    .alloc = subghz_protocol_decoder_honeywell_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_honeywell_feed,
    .reset = subghz_protocol_decoder_honeywell_reset,
    .get_hash_data = subghz_protocol_decoder_honeywell_get_hash_data,
    .serialize = subghz_protocol_decoder_honeywell_serialize,
    .deserialize = subghz_protocol_decoder_honeywell_deserialize,
    .get_string = subghz_protocol_decoder_honeywell_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_honeywell_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_honeywell");

const SubGhzProtocol subghz_protocol_honeywell = {
    .name = SUBGHZ_PROTOCOL_HONEYWELL_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send |
            SubGhzProtocolFlag_Sensors,
    .encoder = &subghz_protocol_honeywell_encoder_ext.vtable,
    .decoder = &subghz_protocol_honeywell_decoder,

};

static void subghz_protocol_decoder_honeywell_addbit(void* context, bool data);

void* subghz_protocol_decoder_honeywell_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderHoneywell* instance = malloc(sizeof(SubGhzProtocolDecoderHoneywell));
    instance->base.protocol = &subghz_protocol_honeywell;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->manchester_saved_state = ManchesterStateMid1;
    return instance;
}

uint16_t subghz_protocol_honeywell_crc16(
    uint8_t const message[],
    unsigned nBytes,
    uint16_t polynomial,
    uint16_t init) {
    uint16_t remainder = init;
    unsigned byte, bit;

    for(byte = 0; byte < nBytes; ++byte) {
        remainder ^= message[byte] << 8;
        for(bit = 0; bit < 8; ++bit) {
            if(remainder & 0x8000) {
                remainder = (remainder << 1) ^ polynomial;
            } else {
                remainder = (remainder << 1);
            }
        }
    }
    return remainder;
}

void subghz_protocol_decoder_honeywell_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderHoneywell* instance = context;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->manchester_saved_state = ManchesterStateMid1;
}

void subghz_protocol_decoder_honeywell_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderHoneywell* instance = context;

    ManchesterEvent event = ManchesterEventReset;
    if(!level) {
        if(DURATION_DIFF(duration, subghz_protocol_honeywell_const.te_short) <
           subghz_protocol_honeywell_const.te_delta) {
            event = ManchesterEventShortLow;
        } else if(
            DURATION_DIFF(duration, subghz_protocol_honeywell_const.te_long) <
            subghz_protocol_honeywell_const.te_delta * 2) {
            event = ManchesterEventLongLow;
        }
    } else {
        if(DURATION_DIFF(duration, subghz_protocol_honeywell_const.te_short) <
           subghz_protocol_honeywell_const.te_delta) {
            event = ManchesterEventShortHigh;
        } else if(
            DURATION_DIFF(duration, subghz_protocol_honeywell_const.te_long) <
            subghz_protocol_honeywell_const.te_delta * 2) {
            event = ManchesterEventLongHigh;
        }
    }
    if(event != ManchesterEventReset) {
        bool data;
        bool data_ok = manchester_advance(
            instance->manchester_saved_state, event, &instance->manchester_saved_state, &data);
        if(data_ok) {
            subghz_protocol_decoder_honeywell_addbit(instance, data);
        }
    } else {
        instance->decoder.decode_data = 0;
        instance->decoder.decode_count_bit = 0;
    }
}

static void subghz_protocol_decoder_honeywell_addbit(void* context, bool data) {
    SubGhzProtocolDecoderHoneywell* instance = context;
    instance->decoder.decode_data = (instance->decoder.decode_data << 1) | data;
    instance->decoder.decode_count_bit++;

    if(instance->decoder.decode_count_bit < 62) {
        return;
    }

    uint16_t preamble = (instance->decoder.decode_data >> 48) & 0xFFFF;

    if(preamble == 0b0011111111111110 || preamble == 0b0111111111111110 ||
       preamble == 0b1111111111111110) {
        uint8_t datatocrc[4];
        datatocrc[0] = (instance->decoder.decode_data >> 40) & 0xFF;
        datatocrc[1] = (instance->decoder.decode_data >> 32) & 0xFF;
        datatocrc[2] = (instance->decoder.decode_data >> 24) & 0xFF;
        datatocrc[3] = (instance->decoder.decode_data >> 16) & 0xFF;
        uint8_t channel = (instance->decoder.decode_data >> 44) & 0xF;
        uint16_t crc_calc = 0;
        if(channel == 0x2 || channel == 0x4 || channel == 0xA) {

            crc_calc = subghz_protocol_honeywell_crc16(datatocrc, 4, 0x8050, 0);
        } else if(channel == 0x8) {
            crc_calc = subghz_protocol_honeywell_crc16(datatocrc, 4, 0x8005, 0);
        } else {
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            return;
        }
        uint16_t crc = instance->decoder.decode_data & 0xFFFF;
        if(crc == crc_calc) {

            instance->generic.data =
                ((((((0xFF << 16) | ((instance->decoder.decode_data >> 40) & 0xFFFF)) << 16) |
                   ((instance->decoder.decode_data >> 24) & 0xFFFF))
                  << 16) |
                 ((instance->decoder.decode_data >> 8) & 0xFFFF))
                    << 8 |
                (instance->decoder.decode_data & 0xFF);
            instance->generic.data_count_bit = 64;
            if(instance->base.callback)
                instance->base.callback(&instance->base, instance->base.context);
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
        } else {
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            return;
        }
    } else if(instance->decoder.decode_count_bit >= 64) {
        instance->decoder.decode_data = 0;
        instance->decoder.decode_count_bit = 0;
        return;
    }
}

uint8_t subghz_protocol_decoder_honeywell_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderHoneywell* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_honeywell_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderHoneywell* instance = context;
    return subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus
    subghz_protocol_decoder_honeywell_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderHoneywell* instance = context;

    SubGhzProtocolStatus res = SubGhzProtocolStatusError;
    res = subghz_block_generic_deserialize(&instance->generic, flipper_format);
    if(res != SubGhzProtocolStatusOk) {
        return res;
    }

    if(instance->generic.data_count_bit != 64) {
        if(instance->generic.data_count_bit < 62) {
            return SubGhzProtocolStatusErrorValueBitCount;
        }

        instance->generic.data =
            ((((((0xFF << 16) | ((instance->generic.data >> 40) & 0xFFFF)) << 16) |
               ((instance->generic.data >> 24) & 0xFFFF))
              << 16) |
             ((instance->generic.data >> 8) & 0xFFFF))
                << 8 |
            (instance->generic.data & 0xFF);
        instance->generic.data_count_bit = 64;
    }

    return res;
}

void subghz_protocol_decoder_honeywell_get_string(void* context, FuriString* output) {
    SubGhzProtocolDecoderHoneywell* instance = context;
    const SubGhzDecodeInput input = {
        .decoder = context,
        .generic = &instance->generic,
        .extra = NULL,
        .extra_count = 0,
        .aux = NULL,
    };
    subghz_decode_plugin_get_string(instance->generic.protocol_name, &input, output);
}

