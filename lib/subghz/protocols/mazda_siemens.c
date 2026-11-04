#include "mazda_siemens.h"

#include "common.h"
#include "../encode_plugin.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolMazdaSiemens"

static const SubGhzBlockConst subghz_protocol_mazda_siemens_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 100,
    .min_count_bit_for_found = 64,
};

#define MAZDA_PREAMBLE_MIN 13
#define MAZDA_COMPLETION_MIN 80
#define MAZDA_COMPLETION_MAX 105
#define MAZDA_DATA_BUFFER_SIZE 14
#define MAZDA_TX_REPEATS 4
#define MAZDA_PREAMBLE_BYTES 12
#define MAZDA_TX_GAP_US 50000

struct SubGhzProtocolDecoderMazdaSiemens {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t preamble_count;
    uint16_t bit_counter;
    uint8_t prev_state;
    uint8_t data_buffer[MAZDA_DATA_BUFFER_SIZE];
};

struct SubGhzProtocolEncoderMazdaSiemens {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderMazdaSiemens);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderMazdaSiemens);

typedef enum {
    MazdaSiemensDecoderStepReset = 0,
    MazdaSiemensDecoderStepPreambleSave,
    MazdaSiemensDecoderStepPreambleCheck,
    MazdaSiemensDecoderStepDataSave,
    MazdaSiemensDecoderStepDataCheck,
} MazdaSiemensDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_mazda_siemens_decoder = {
    .alloc = subghz_protocol_decoder_mazda_siemens_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_mazda_siemens_feed,
    .reset = subghz_protocol_decoder_mazda_siemens_reset,

    .get_hash_data = subghz_protocol_decoder_mazda_siemens_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_mazda_siemens_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_mazda_siemens_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_mazda_siemens");

const SubGhzProtocol subghz_protocol_mazda_siemens = {
    .name = SUBGHZ_PROTOCOL_MAZDA_SIEMENS_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_mazda_siemens_decoder,
    .encoder = &subghz_protocol_mazda_siemens_encoder_ext.vtable,
};

static uint8_t mazda_byte_parity(uint8_t val);
static void mazda_xor_deobfuscate(uint8_t* data);

static inline bool mazda_is_short(uint32_t duration) {
    return DURATION_DIFF(duration, subghz_protocol_mazda_siemens_const.te_short) <
        subghz_protocol_mazda_siemens_const.te_delta;
}

static inline bool mazda_is_long(uint32_t duration) {
    return DURATION_DIFF(duration, subghz_protocol_mazda_siemens_const.te_long) <
        subghz_protocol_mazda_siemens_const.te_delta;
}

static void mazda_collect_bit(SubGhzProtocolDecoderMazdaSiemens* instance, uint8_t state_bit) {
    uint8_t byte_idx = instance->bit_counter >> 3;
    if(byte_idx < MAZDA_DATA_BUFFER_SIZE) {
        instance->data_buffer[byte_idx] <<= 1;
        if(state_bit == 0) {
            instance->data_buffer[byte_idx] |= 1;
        }
    }
    instance->bit_counter++;
}

static bool mazda_check_completion(SubGhzProtocolDecoderMazdaSiemens* instance) {
    if(instance->bit_counter < MAZDA_COMPLETION_MIN ||
    instance->bit_counter > MAZDA_COMPLETION_MAX) {
        return false;
    }

    uint8_t data[8];
    for(int i = 0; i < 8; i++) {
        data[i] = instance->data_buffer[i + 1];
    }

    mazda_xor_deobfuscate(data);

    uint8_t checksum = 0;
    for(int i = 0; i < 7; i++) {
        checksum += data[i];
    }
    if(checksum != data[7]) {
        return false;
    }

    uint64_t packed = 0;
    for(int i = 0; i < 8; i++) {
        packed = (packed << 8) | data[i];
    }

    instance->generic.data = packed;
    instance->generic.data_count_bit = 64;
    return true;
}

static uint8_t mazda_byte_parity(uint8_t val) {
    val ^= val >> 4;
    val ^= val >> 2;
    val ^= val >> 1;
    return val & 1;
}

static void mazda_xor_deobfuscate(uint8_t* data) {
    uint8_t parity = mazda_byte_parity(data[7]);

    if(parity) {

        uint8_t mask = data[6];
        for(int i = 0; i < 6; i++) {
            data[i] ^= mask;
        }
    } else {

        uint8_t mask = data[5];
        for(int i = 0; i < 5; i++) {
            data[i] ^= mask;
        }
        data[6] ^= mask;
    }

    uint8_t old5 = data[5];
    uint8_t old6 = data[6];
    data[5] = (old5 & 0xAA) | (old6 & 0x55);
    data[6] = (old5 & 0x55) | (old6 & 0xAA);
}

#define MAZDA_UPLOAD_MAX 400

void* subghz_protocol_decoder_mazda_siemens_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderMazdaSiemens), &subghz_protocol_mazda_siemens);
}

void subghz_protocol_decoder_mazda_siemens_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderMazdaSiemens* instance = context;
    instance->decoder.parser_step = MazdaSiemensDecoderStepReset;
    instance->preamble_count = 0;
    instance->bit_counter = 0;
    instance->prev_state = 0;
    memset(instance->data_buffer, 0, MAZDA_DATA_BUFFER_SIZE);
}

static bool mazda_process_pair(
    SubGhzProtocolDecoderMazdaSiemens* instance,
    uint32_t dur_first,
    uint32_t dur_second) {
    bool first_short = mazda_is_short(dur_first);
    bool first_long = mazda_is_long(dur_first);
    bool second_short = mazda_is_short(dur_second);
    bool second_long = mazda_is_long(dur_second);

    if(first_long && second_short) {
        mazda_collect_bit(instance, 0);
        mazda_collect_bit(instance, 1);
        instance->prev_state = 1;
        return true;
    }

    if(first_short && second_long) {
        mazda_collect_bit(instance, 1);
        instance->prev_state = 0;
        return true;
    }

    if(first_short && second_short) {
        mazda_collect_bit(instance, instance->prev_state);
        return true;
    }

    if(first_long && second_long) {
        mazda_collect_bit(instance, 0);
        mazda_collect_bit(instance, 1);
        instance->prev_state = 0;
        return true;
    }

    return false;
}

void subghz_protocol_decoder_mazda_siemens_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    UNUSED(level);
    SubGhzProtocolDecoderMazdaSiemens* instance = context;

    switch(instance->decoder.parser_step) {
    case MazdaSiemensDecoderStepReset:
        if(mazda_is_short(duration)) {
            instance->decoder.te_last = duration;
            instance->preamble_count = 0;
            instance->decoder.parser_step = MazdaSiemensDecoderStepPreambleCheck;
        }
        break;

    case MazdaSiemensDecoderStepPreambleSave:
        instance->decoder.te_last = duration;
        instance->decoder.parser_step = MazdaSiemensDecoderStepPreambleCheck;
        break;

    case MazdaSiemensDecoderStepPreambleCheck:
        if(mazda_is_short(instance->decoder.te_last) && mazda_is_short(duration)) {
            instance->preamble_count++;
            instance->decoder.parser_step = MazdaSiemensDecoderStepPreambleSave;
        } else if(
            mazda_is_short(instance->decoder.te_last) && mazda_is_long(duration) &&
            instance->preamble_count >= MAZDA_PREAMBLE_MIN) {

            instance->bit_counter = 1;
            memset(instance->data_buffer, 0, MAZDA_DATA_BUFFER_SIZE);

            mazda_collect_bit(instance, 1);
            instance->prev_state = 0;

            instance->decoder.parser_step = MazdaSiemensDecoderStepDataSave;
        } else {
            instance->decoder.parser_step = MazdaSiemensDecoderStepReset;
        }
        break;

    case MazdaSiemensDecoderStepDataSave:
        instance->decoder.te_last = duration;
        instance->decoder.parser_step = MazdaSiemensDecoderStepDataCheck;
        break;

    case MazdaSiemensDecoderStepDataCheck:
        if(mazda_process_pair(instance, instance->decoder.te_last, duration)) {
            instance->decoder.parser_step = MazdaSiemensDecoderStepDataSave;
        } else {
            if(mazda_check_completion(instance)) {
                if(instance->base.callback) {
                    instance->base.callback(&instance->base, instance->base.context);
                }
            }
            instance->decoder.parser_step = MazdaSiemensDecoderStepReset;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_mazda_siemens_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderMazdaSiemens* instance = context;
    return (uint8_t)(instance->generic.data ^ (instance->generic.data >> 8) ^
                    (instance->generic.data >> 16) ^ (instance->generic.data >> 24) ^
                    (instance->generic.data >> 32) ^ (instance->generic.data >> 40) ^
                    (instance->generic.data >> 48) ^ (instance->generic.data >> 56));
}

SubGhzProtocolStatus
    subghz_protocol_decoder_mazda_siemens_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderMazdaSiemens* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_mazda_siemens_const.min_count_bit_for_found);
}

