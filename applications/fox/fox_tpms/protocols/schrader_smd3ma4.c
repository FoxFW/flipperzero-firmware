#include "schrader_smd3ma4.h"
#include <lib/subghz/blocks/generic.h>
#include "../helpers/fox_tpms_type_compat.h"
#include "../helpers/fox_tpms_protocol_flag_compat.h"
#include <lib/toolbox/manchester_decoder.h>

#define TAG "SchraderSMD3MA4"

#define PREAMBLE_HIGH       0xF5555555
#define PREAMBLE_LOW_NIBBLE 0xE
#define PREAMBLE_BITS_LEN   36
#define DATA_BITS_LEN       38

static const SubGhzBlockConst tpms_protocol_schrader_smd3ma4_const = {
    .te_short = 120,
    .te_long = 240,
    .te_delta = 60,
    .min_count_bit_for_found = 35,
};

struct TPMSProtocolDecoderSchraderSMD3MA4 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    TPMSBlockGeneric generic;

    ManchesterState manchester_saved_state;
    uint16_t header_count;
    uint64_t preamble_data;
};

struct TPMSProtocolEncoderSchraderSMD3MA4 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    TPMSBlockGeneric generic;
};

typedef enum {
    SchraderSMD3MA4DecoderStepReset = 0,
    SchraderSMD3MA4DecoderStepCheckPreamble,
    SchraderSMD3MA4DecoderStepDecoderData,
} SchraderSMD3MA4DecoderStep;

const SubGhzProtocolDecoder tpms_protocol_schrader_smd3ma4_decoder = {
    .alloc = tpms_protocol_decoder_schrader_smd3ma4_alloc,
    .free = tpms_protocol_decoder_schrader_smd3ma4_free,

    .feed = tpms_protocol_decoder_schrader_smd3ma4_feed,
    .reset = tpms_protocol_decoder_schrader_smd3ma4_reset,

    .get_hash_data = tpms_protocol_decoder_schrader_smd3ma4_get_hash_data,
    .serialize = tpms_protocol_decoder_schrader_smd3ma4_serialize,
    .deserialize = tpms_protocol_decoder_schrader_smd3ma4_deserialize,
    .get_string = tpms_protocol_decoder_schrader_smd3ma4_get_string,
};

const SubGhzProtocolEncoder tpms_protocol_schrader_smd3ma4_encoder = {
    .alloc = NULL,
    .free = NULL,

    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

const SubGhzProtocol tpms_protocol_schrader_smd3ma4 = {
    .name = TPMS_PROTOCOL_SCHRADER_SMD3MA4_NAME,
    .type = FOX_TPMS_PROTOCOL_TYPE,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable |
            FOX_TPMS_FLAG_SENSORS | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Load,

    .decoder = &tpms_protocol_schrader_smd3ma4_decoder,
    .encoder = &tpms_protocol_schrader_smd3ma4_encoder,
};

void* tpms_protocol_decoder_schrader_smd3ma4_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    TPMSProtocolDecoderSchraderSMD3MA4* instance =
        malloc(sizeof(TPMSProtocolDecoderSchraderSMD3MA4));
    instance->base.protocol = &tpms_protocol_schrader_smd3ma4;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void tpms_protocol_decoder_schrader_smd3ma4_free(void* context) {
    furi_assert(context);
    TPMSProtocolDecoderSchraderSMD3MA4* instance = context;
    free(instance);
}

void tpms_protocol_decoder_schrader_smd3ma4_reset(void* context) {
    furi_assert(context);
    TPMSProtocolDecoderSchraderSMD3MA4* instance = context;
    instance->decoder.parser_step = SchraderSMD3MA4DecoderStepReset;
    instance->preamble_data = 0;
    instance->header_count = 0;
}

static void tpms_protocol_schrader_smd3ma4_analyze(TPMSBlockGeneric* instance) {

    uint8_t b[5];
    b[0] = (instance->data >> 32) & 0xFF;
    b[1] = (instance->data >> 24) & 0xFF;
    b[2] = (instance->data >> 16) & 0xFF;
    b[3] = (instance->data >> 8) & 0xFF;
    b[4] = instance->data & 0xFF;

    instance->id = ((uint32_t)(b[0] & 0x1f) << 19) | ((uint32_t)b[1] << 11) |
                   ((uint32_t)b[2] << 3) | ((uint32_t)b[3] >> 5);

    uint8_t pressure_raw = ((b[3] & 0x1f) << 3) | (b[4] >> 5);

    float pressure_psi = pressure_raw * 0.2f;
    instance->pressure = pressure_psi * 0.0689476f;

    instance->temperature = 0;

    instance->battery_low = TPMS_NO_BATT;
}

static ManchesterEvent level_and_duration_to_event(bool level, uint32_t duration) {
    bool is_long = false;

    if(DURATION_DIFF(duration, tpms_protocol_schrader_smd3ma4_const.te_long) <
       tpms_protocol_schrader_smd3ma4_const.te_delta) {
        is_long = true;
    } else if(
        DURATION_DIFF(duration, tpms_protocol_schrader_smd3ma4_const.te_short) <
        tpms_protocol_schrader_smd3ma4_const.te_delta) {
        is_long = false;
    } else {
        return ManchesterEventReset;
    }

    if(level)
        return is_long ? ManchesterEventLongHigh : ManchesterEventShortHigh;
    else
        return is_long ? ManchesterEventLongLow : ManchesterEventShortLow;
}

void tpms_protocol_decoder_schrader_smd3ma4_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    TPMSProtocolDecoderSchraderSMD3MA4* instance = context;

    uint16_t pulse_count = (duration + tpms_protocol_schrader_smd3ma4_const.te_short / 2) /
                           tpms_protocol_schrader_smd3ma4_const.te_short;

    switch(instance->decoder.parser_step) {
    case SchraderSMD3MA4DecoderStepReset:

        if(level && pulse_count >= 3) {
            instance->decoder.parser_step = SchraderSMD3MA4DecoderStepCheckPreamble;
            instance->preamble_data = 0;
            instance->header_count = 0;

            for(uint16_t i = 0; i < pulse_count && i < 4; i++) {
                instance->preamble_data = (instance->preamble_data << 1) | 1;
                instance->header_count++;
            }
        }
        break;

    case SchraderSMD3MA4DecoderStepCheckPreamble:

        for(uint16_t i = 0; i < pulse_count && instance->header_count < PREAMBLE_BITS_LEN; i++) {
            instance->preamble_data = (instance->preamble_data << 1) | (level ? 1 : 0);
            instance->header_count++;
        }

        if(instance->header_count >= PREAMBLE_BITS_LEN) {

            uint64_t expected_preamble = 0xF5555555EULL;
            uint64_t mask = (1ULL << PREAMBLE_BITS_LEN) - 1;

            if((instance->preamble_data & mask) == expected_preamble) {
                FURI_LOG_D(TAG, "Preamble matched: %llx", instance->preamble_data);
                instance->decoder.parser_step = SchraderSMD3MA4DecoderStepDecoderData;
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->manchester_saved_state = ManchesterStateStart1;
            } else {
                FURI_LOG_D(TAG, "Preamble mismatch: %llx", instance->preamble_data);
                instance->decoder.parser_step = SchraderSMD3MA4DecoderStepReset;
            }
        }
        break;

    case SchraderSMD3MA4DecoderStepDecoderData: {

        ManchesterEvent event = level_and_duration_to_event(level, duration);

        if(event == ManchesterEventReset) {

            if(instance->decoder.decode_count_bit >=
               tpms_protocol_schrader_smd3ma4_const.min_count_bit_for_found) {
                FURI_LOG_D(
                    TAG,
                    "Decode complete: %d bits, data: %llx",
                    instance->decoder.decode_count_bit,
                    instance->decoder.decode_data);

                instance->generic.data = instance->decoder.decode_data;
                instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                tpms_protocol_schrader_smd3ma4_analyze(&instance->generic);

                if(instance->generic.id != 0 || instance->generic.pressure != 0) {
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
            }
            instance->decoder.parser_step = SchraderSMD3MA4DecoderStepReset;
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

uint8_t tpms_protocol_decoder_schrader_smd3ma4_get_hash_data(void* context) {
    furi_assert(context);
    TPMSProtocolDecoderSchraderSMD3MA4* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus tpms_protocol_decoder_schrader_smd3ma4_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    TPMSProtocolDecoderSchraderSMD3MA4* instance = context;
    return tpms_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus tpms_protocol_decoder_schrader_smd3ma4_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_assert(context);
    TPMSProtocolDecoderSchraderSMD3MA4* instance = context;
    return tpms_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        tpms_protocol_schrader_smd3ma4_const.min_count_bit_for_found);
}

void tpms_protocol_decoder_schrader_smd3ma4_get_string(void* context, FuriString* output) {
    furi_assert(context);
    TPMSProtocolDecoderSchraderSMD3MA4* instance = context;

    float pressure_psi = instance->generic.pressure / 0.0689476f;

    furi_string_printf(
        output,
        "%s\r\n"
        "Id:0x%06lX\r\n"
        "Bat:%s\r\n"
        "Pressure:%2.1f PSI\r\n"
        "         %2.2f Bar",
        instance->generic.protocol_name,
        instance->generic.id,
        (instance->generic.battery_low == TPMS_NO_BATT) ?
            "N/A" :
            (instance->generic.battery_low ? "Low" : "OK"),
        (double)pressure_psi,
        (double)instance->generic.pressure);
}
