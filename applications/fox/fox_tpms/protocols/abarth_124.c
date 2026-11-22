#include "abarth_124.h"
#include <lib/subghz/blocks/generic.h>
#include "../helpers/fox_tpms_type_compat.h"
#include "../helpers/fox_tpms_protocol_flag_compat.h"
#include <lib/toolbox/manchester_decoder.h>

#define TAG "Abarth124"

#define PREAMBLE_PATTERN  0xAAA9
#define PREAMBLE_BITS_LEN 16
#define DATA_BITS_LEN     72

static const SubGhzBlockConst tpms_protocol_abarth_124_const = {
    .te_short = 52,
    .te_long = 104,
    .te_delta = 25,
    .min_count_bit_for_found = 72,
};

struct TPMSProtocolDecoderAbarth124 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    TPMSBlockGeneric generic;

    ManchesterState manchester_saved_state;
    uint16_t header_count;
    uint32_t preamble_data;
};

struct TPMSProtocolEncoderAbarth124 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    TPMSBlockGeneric generic;
};

typedef enum {
    Abarth124DecoderStepReset = 0,
    Abarth124DecoderStepCheckPreamble,
    Abarth124DecoderStepDecoderData,
} Abarth124DecoderStep;

const SubGhzProtocolDecoder tpms_protocol_abarth_124_decoder = {
    .alloc = tpms_protocol_decoder_abarth_124_alloc,
    .free = tpms_protocol_decoder_abarth_124_free,

    .feed = tpms_protocol_decoder_abarth_124_feed,
    .reset = tpms_protocol_decoder_abarth_124_reset,

    .get_hash_data = tpms_protocol_decoder_abarth_124_get_hash_data,
    .serialize = tpms_protocol_decoder_abarth_124_serialize,
    .deserialize = tpms_protocol_decoder_abarth_124_deserialize,
    .get_string = tpms_protocol_decoder_abarth_124_get_string,
};

const SubGhzProtocolEncoder tpms_protocol_abarth_124_encoder = {
    .alloc = NULL,
    .free = NULL,

    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

const SubGhzProtocol tpms_protocol_abarth_124 = {
    .name = TPMS_PROTOCOL_ABARTH_124_NAME,
    .type = FOX_TPMS_PROTOCOL_TYPE,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            FOX_TPMS_FLAG_SENSORS | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Load,

    .decoder = &tpms_protocol_abarth_124_decoder,
    .encoder = &tpms_protocol_abarth_124_encoder,
};

void* tpms_protocol_decoder_abarth_124_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    TPMSProtocolDecoderAbarth124* instance = malloc(sizeof(TPMSProtocolDecoderAbarth124));
    instance->base.protocol = &tpms_protocol_abarth_124;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void tpms_protocol_decoder_abarth_124_free(void* context) {
    furi_assert(context);
    TPMSProtocolDecoderAbarth124* instance = context;
    free(instance);
}

void tpms_protocol_decoder_abarth_124_reset(void* context) {
    furi_assert(context);
    TPMSProtocolDecoderAbarth124* instance = context;
    instance->decoder.parser_step = Abarth124DecoderStepReset;
    instance->preamble_data = 0;
    instance->header_count = 0;
}

static bool tpms_protocol_abarth_124_check_checksum(TPMSProtocolDecoderAbarth124* instance) {
    uint64_t data = instance->decoder.decode_data;

    uint8_t b[8];
    b[0] = (data >> 56) & 0xFF;
    b[1] = (data >> 48) & 0xFF;
    b[2] = (data >> 40) & 0xFF;
    b[3] = (data >> 32) & 0xFF;
    b[4] = (data >> 24) & 0xFF;
    b[5] = (data >> 16) & 0xFF;
    b[6] = (data >> 8) & 0xFF;
    b[7] = data & 0xFF;

    uint8_t checksum = 0;
    for(int i = 0; i < 7; i++) {
        checksum ^= b[i];
    }

    if(checksum == b[7]) {
        return true;
    }

    uint8_t pressure = b[4];
    uint8_t temp = b[5];

    if(pressure >= 50 && pressure <= 253 && temp >= 20 && temp <= 150) {
        FURI_LOG_D(TAG, "Checksum mismatch but data looks valid, accepting");
        return true;
    }

    return false;
}

static void tpms_protocol_abarth_124_analyze(TPMSBlockGeneric* instance) {
    uint64_t data = instance->data;

    uint32_t id_partial = (data >> 32) & 0x00FFFFFF;
    instance->id = id_partial;

    uint8_t pressure_raw = (data >> 24) & 0xFF;

    float pressure_kpa = pressure_raw * 1.38f;
    instance->pressure = pressure_kpa * 0.01f;

    uint8_t temp_raw = (data >> 16) & 0xFF;
    instance->temperature = (float)(temp_raw - 50);

    instance->battery_low = TPMS_NO_BATT;
}

static ManchesterEvent level_and_duration_to_event(bool level, uint32_t duration) {
    bool is_long = false;

    if(DURATION_DIFF(duration, tpms_protocol_abarth_124_const.te_long) <
       tpms_protocol_abarth_124_const.te_delta) {
        is_long = true;
    } else if(
        DURATION_DIFF(duration, tpms_protocol_abarth_124_const.te_short) <
        tpms_protocol_abarth_124_const.te_delta) {
        is_long = false;
    } else {
        return ManchesterEventReset;
    }

    if(level)
        return is_long ? ManchesterEventLongHigh : ManchesterEventShortHigh;
    else
        return is_long ? ManchesterEventLongLow : ManchesterEventShortLow;
}

void tpms_protocol_decoder_abarth_124_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    TPMSProtocolDecoderAbarth124* instance = context;

    uint16_t pulse_count = (duration + tpms_protocol_abarth_124_const.te_short / 2) /
                           tpms_protocol_abarth_124_const.te_short;

    switch(instance->decoder.parser_step) {
    case Abarth124DecoderStepReset:

        if(pulse_count >= 2) {
            instance->decoder.parser_step = Abarth124DecoderStepCheckPreamble;
            instance->preamble_data = 0;
            instance->header_count = 0;

            for(uint16_t i = 0; i < pulse_count && i < 4; i++) {
                instance->preamble_data = (instance->preamble_data << 1) | (level ? 1 : 0);
                instance->header_count++;
            }
        }
        break;

    case Abarth124DecoderStepCheckPreamble:

        for(uint16_t i = 0; i < pulse_count && instance->header_count < PREAMBLE_BITS_LEN; i++) {
            instance->preamble_data = (instance->preamble_data << 1) | (level ? 1 : 0);
            instance->header_count++;
        }

        if(instance->header_count >= PREAMBLE_BITS_LEN) {

            uint16_t preamble = instance->preamble_data & 0xFFFF;

            if(preamble == PREAMBLE_PATTERN || preamble == (uint16_t)~PREAMBLE_PATTERN) {
                FURI_LOG_D(TAG, "Preamble matched: %04x", preamble);
                instance->decoder.parser_step = Abarth124DecoderStepDecoderData;
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->manchester_saved_state = ManchesterStateStart1;
            } else {
                FURI_LOG_D(TAG, "Preamble mismatch: %04lx", instance->preamble_data);
                instance->decoder.parser_step = Abarth124DecoderStepReset;
            }
        }
        break;

    case Abarth124DecoderStepDecoderData: {

        ManchesterEvent event = level_and_duration_to_event(level, duration);

        if(event == ManchesterEventReset) {

            if(instance->decoder.decode_count_bit >=
               tpms_protocol_abarth_124_const.min_count_bit_for_found) {
                FURI_LOG_D(
                    TAG,
                    "Decode complete: %d bits, data: %llx",
                    instance->decoder.decode_count_bit,
                    instance->decoder.decode_data);

                if(tpms_protocol_abarth_124_check_checksum(instance)) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                    tpms_protocol_abarth_124_analyze(&instance->generic);

                    if(instance->generic.id != 0) {
                        if(instance->base.callback)
                            instance->base.callback(&instance->base, instance->base.context);
                    }
                } else {
                    FURI_LOG_D(TAG, "Checksum failed");
                }
            }
            instance->decoder.parser_step = Abarth124DecoderStepReset;
        } else {
            bool bit = false;
            bool have_bit = manchester_advance(
                instance->manchester_saved_state, event, &instance->manchester_saved_state, &bit);

            if(have_bit) {

                bit = !bit;
                subghz_protocol_blocks_add_bit(&instance->decoder, bit);
            }
        }
        break;
    }
    }
}

uint8_t tpms_protocol_decoder_abarth_124_get_hash_data(void* context) {
    furi_assert(context);
    TPMSProtocolDecoderAbarth124* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus tpms_protocol_decoder_abarth_124_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    TPMSProtocolDecoderAbarth124* instance = context;
    return tpms_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus
    tpms_protocol_decoder_abarth_124_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    TPMSProtocolDecoderAbarth124* instance = context;
    return tpms_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        tpms_protocol_abarth_124_const.min_count_bit_for_found);
}

void tpms_protocol_decoder_abarth_124_get_string(void* context, FuriString* output) {
    furi_assert(context);
    TPMSProtocolDecoderAbarth124* instance = context;

    float pressure_psi = instance->generic.pressure * 14.5038f;

    float pressure_kpa = instance->generic.pressure * 100.0f;

    furi_string_printf(
        output,
        "%s\r\n"
        "Id:0x%08lX\r\n"
        "Bat:%s\r\n"
        "Temp:%2.0f C\r\n"
        "Pressure:%2.1f PSI\r\n"
        "         %3.0f kPa",
        instance->generic.protocol_name,
        instance->generic.id,
        (instance->generic.battery_low == TPMS_NO_BATT) ?
            "N/A" :
            (instance->generic.battery_low ? "Low" : "OK"),
        (double)instance->generic.temperature,
        (double)pressure_psi,
        (double)pressure_kpa);
}
