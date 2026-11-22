#include "mic6sc2.h"
#include "../encode_plugin.h"

#include <furi.h>
#include <string.h>
#include <lib/subghz/types.h>
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>
#include <flipper_format/flipper_format.h>

#ifndef ENABLE_EMULATE_FEATURE
#define ENABLE_EMULATE_FEATURE 1
#endif

#ifndef PP_COMPAT_SHIM_DEFINED
#define PP_COMPAT_SHIM_DEFINED
#define FF_BIT                    "Bit"
#define FF_KEY                    "Key"
#define FF_SERIAL                 "Serial"
#define FF_BTN                    "Btn"
#define FF_CNT                    "Cnt"
#define FF_REPEAT                 "Repeat"
#define FF_PROTOCOL               "Protocol"
#define FF_TYPE                   "Type"
#define PP_FIELD_SERIAL           0x01U
#define PP_FIELD_BTN              0x02U
#define PP_FIELD_CNT              0x04U
#define PP_FIELD_TYPE             0x08U
#define PP_ENCODER_REPEAT_MAX     50U
#define PP_SHARED_UPLOAD_CAPACITY 2048U

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
} ProtoPirateDecoderHeader;

typedef struct {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
} ProtoPirateEncoderHeader;

#endif

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include "decode_plugin.h"
#include "pp_auto_common.h"

#define TAG "MIC6SC2"

static const SubGhzBlockConst subghz_protocol_mic6sc2_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 100,
    .min_count_bit_for_found = 64,
};

#define MIC6SC2_MAX_BYTES 11U
#define MIC6SC2_MAX_BITS  88U
#define MIC6SC2_MIN_BITS  80U

typedef struct SubGhzProtocolDecoderMic6sc2 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;

    uint8_t frame[MIC6SC2_MAX_BYTES];
    uint16_t bit_count;

    uint32_t encrypted;
    uint16_t sequence;
    uint8_t button;
} SubGhzProtocolDecoderMic6sc2;

const SubGhzProtocolDecoder subghz_protocol_mic6sc2_decoder = {
    .alloc = subghz_protocol_decoder_mic6sc2_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_mic6sc2_feed,
    .reset = subghz_protocol_decoder_mic6sc2_reset,
    .get_hash_data = subghz_protocol_decoder_mic6sc2_get_hash_data,
    .serialize = subghz_protocol_decoder_mic6sc2_serialize,
    .deserialize = subghz_protocol_decoder_mic6sc2_deserialize,
    .get_string = subghz_protocol_decoder_mic6sc2_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_mic6sc2_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

static const SubGhzEncoderExternal subghz_protocol_mic6sc2_encoder_ext =
    SUBGHZ_ENCODER_EXTERNAL("enc_recon_manchester");

const SubGhzProtocol subghz_protocol_mic6sc2 = {
    .name = MIC6SC2_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_mic6sc2_decoder,
    .encoder = &subghz_protocol_mic6sc2_encoder_ext.vtable,
};

static void mic6sc2_soft_reset(SubGhzProtocolDecoderMic6sc2* instance) {
    memset(instance->frame, 0, sizeof(instance->frame));
    instance->bit_count = 0;
    manchester_advance(
        instance->manchester_state, ManchesterEventReset, &instance->manchester_state, NULL);
}

static bool mic6sc2_process_frame(SubGhzProtocolDecoderMic6sc2* instance) {
    if(instance->bit_count < MIC6SC2_MIN_BITS) return false;

    const uint8_t* b = instance->frame;

    if(b[0] != 0x55 || b[1] != 0x54) return false;

    uint8_t xor_sum = 0;
    for(uint8_t i = 2; i < 11; i++) {
        xor_sum ^= b[i];
    }
    if(xor_sum != 0) return false;

    instance->encrypted = ((uint32_t)ppa_reverse_bits8(b[5]) << 24) |
                          ((uint32_t)ppa_reverse_bits8(b[4]) << 16) |
                          ((uint32_t)ppa_reverse_bits8(b[3]) << 8) |
                          (uint32_t)ppa_reverse_bits8(b[2]);
    instance->button = ppa_reverse_bits8(b[6]) & 0x0F;
    instance->sequence =
        (uint16_t)((ppa_reverse_bits8(b[8]) << 8) | ppa_reverse_bits8(b[7]));

    uint64_t data = 0;
    for(uint8_t i = 2; i < 10; i++) {
        data = (data << 8) | b[i];
    }

    instance->generic.data = data;
    instance->generic.data_count_bit = 64;
    instance->generic.serial = instance->encrypted;
    instance->generic.btn = instance->button;
    instance->generic.cnt = instance->sequence;

    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = 64;

    FURI_LOG_D(
        TAG,
        "Frame: %ubit Enc:%08lX Btn:%01X Seq:%04X",
        instance->bit_count,
        (unsigned long)instance->encrypted,
        instance->button,
        instance->sequence);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
    return true;
}

void* subghz_protocol_decoder_mic6sc2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderMic6sc2* instance = calloc(1, sizeof(SubGhzProtocolDecoderMic6sc2));
    instance->base.protocol = &subghz_protocol_mic6sc2;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_mic6sc2_free(void* context) {
    furi_check(context);
    free(context);
}

void subghz_protocol_decoder_mic6sc2_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderMic6sc2* instance = context;
    instance->manchester_state = ManchesterStateMid1;
    memset(instance->frame, 0, sizeof(instance->frame));
    instance->bit_count = 0;
    instance->encrypted = 0;
    instance->sequence = 0;
    instance->button = 0;
}

void subghz_protocol_decoder_mic6sc2_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderMic6sc2* instance = context;

    ManchesterEvent event =
        ppa_manchester_event(duration, level, &subghz_protocol_mic6sc2_const);

    if(event == ManchesterEventReset) {

        mic6sc2_process_frame(instance);
        mic6sc2_soft_reset(instance);
        return;
    }

    bool data_bit;
    if(!manchester_advance(
           instance->manchester_state, event, &instance->manchester_state, &data_bit)) {
        return;
    }

    if(instance->bit_count < MIC6SC2_MAX_BITS) {
        if(data_bit) {
            instance->frame[instance->bit_count >> 3] |=
                (uint8_t)(1U << (7U - (instance->bit_count & 7U)));
        }
        instance->bit_count++;
    }

    if(instance->bit_count == 16) {
        if(instance->frame[0] != 0x55 || instance->frame[1] != 0x54) {
            mic6sc2_soft_reset(instance);
            return;
        }
    }

    if(instance->bit_count >= MIC6SC2_MAX_BITS) {
        mic6sc2_process_frame(instance);
        mic6sc2_soft_reset(instance);
    }
}

uint8_t subghz_protocol_decoder_mic6sc2_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderMic6sc2* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8U) + 1U);
}

SubGhzProtocolStatus subghz_protocol_decoder_mic6sc2_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderMic6sc2* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    if(ret == SubGhzProtocolStatusOk) {
        ppa_serialize_fields(
            flipper_format,
            PP_FIELD_SERIAL | PP_FIELD_BTN | PP_FIELD_CNT,
            instance->generic.serial,
            instance->generic.btn,
            instance->generic.cnt,
            0);
    }

    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_mic6sc2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderMic6sc2* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_mic6sc2_const.min_count_bit_for_found);

    if(ret == SubGhzProtocolStatusOk) {

        uint64_t data = instance->generic.data;
        uint8_t b[8];
        for(int i = 7; i >= 0; i--) {
            b[i] = (uint8_t)(data & 0xFF);
            data >>= 8;
        }

        instance->encrypted = ((uint32_t)ppa_reverse_bits8(b[3]) << 24) |
                              ((uint32_t)ppa_reverse_bits8(b[2]) << 16) |
                              ((uint32_t)ppa_reverse_bits8(b[1]) << 8) |
                              (uint32_t)ppa_reverse_bits8(b[0]);
        instance->button = ppa_reverse_bits8(b[4]) & 0x0F;
        instance->sequence =
            (uint16_t)((ppa_reverse_bits8(b[6]) << 8) | ppa_reverse_bits8(b[5]));

        uint32_t serial = instance->generic.serial;
        uint32_t btn = instance->generic.btn;
        uint32_t cnt = instance->generic.cnt;
        ppa_encoder_read_fields(flipper_format, &serial, &btn, &cnt, NULL);
        instance->generic.serial = serial;
        instance->generic.btn = (uint8_t)btn;
        instance->generic.cnt = cnt;
        instance->encrypted = serial;
        instance->button = (uint8_t)btn;
        instance->sequence = (uint16_t)cnt;
    }

    return ret;
}

void subghz_protocol_decoder_mic6sc2_get_string(void* context, FuriString* output) {
    SubGhzProtocolDecoderMic6sc2* instance = context;
    const SubGhzDecodeInput input = {
        .decoder = context,
        .generic = &instance->generic,
        .extra = NULL,
        .extra_count = 0,
        .aux = NULL,
    };
    subghz_decode_plugin_get_string(instance->generic.protocol_name, &input, output);
}

