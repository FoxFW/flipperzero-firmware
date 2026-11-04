/* enc_nero_radio.fal : SD encoder for nero_radio (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_alloc enc_nero_radio__subghz_protocol_encoder_common_alloc
#define subghz_protocol_encoder_common_free enc_nero_radio__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_nero_radio__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_nero_radio__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_nero_radio_alloc enc_nero_radio__subghz_protocol_encoder_nero_radio_alloc
#define subghz_protocol_encoder_nero_radio_deserialize enc_nero_radio__subghz_protocol_encoder_nero_radio_deserialize
#define subghz_protocol_nero_radio enc_nero_radio__subghz_protocol_nero_radio
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/nero_radio.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolNeroRadio"
#define NERO_RADIO_DYN_COUNT_BIT   66
#define NERO_RADIO_DYN_TE          208
#define NERO_RADIO_DYN_FRAME_BITS  251
#define NERO_RADIO_DYN_HEADER      0x54C // 10101001100
#define NERO_RADIO_DYN_HEADER_BITS 11
#define NERO_RADIO_DYN_SYNC_WORDS  4
#define NERO_RADIO_DYN_DATA_WORDS  11
#define NERO_RADIO_DYN_PREAMBLE    34
#define NERO_RADIO_DYN_CARRIER     128700
#define NERO_RADIO_DYN_GAP         3000
#define NERO_RADIO_NORM_COUNT_BIT  72
#define NERO_RADIO_NORM_HEADER     0xE24 // 111000100100
#define NERO_RADIO_NORM_HEAD_BITS  12
#define NERO_RADIO_NORM_ID_BITS    52
#define NERO_RADIO_NORM_ID_PREAM   8
#define NERO_RADIO_NORM_PREAMBLE   47
#define NERO_RADIO_UPLOAD_CAPACITY 440

struct SubGhzProtocolEncoderNeroRadio {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

typedef struct {
    LevelDuration* upload;
    size_t index;
    bool level;
    uint32_t run;
} NeroRadioChipWriter;

static const SubGhzBlockConst subghz_protocol_nero_radio_const = {
    .te_short = 200,
    .te_long = 400,
    .te_delta = 80,
    .min_count_bit_for_found = 56,
};

static const uint16_t nero_radio_dyn_sync[NERO_RADIO_DYN_SYNC_WORDS] = {
    0x896C,
    0x7435,
    0xCF7C,
    0x117B,
};

static const uint16_t nero_radio_dyn_symbols[64] = {
    0x117B, 0x12BD, 0x13C7, 0x14F6, 0x172E, 0x1D65, 0x1D8B, 0x1E53, 0x1ECC, 0x22DE, 0x236D,
    0x2597, 0x25EA, 0x26F1, 0x29BC, 0x2E1D, 0x2E66, 0x33B2, 0x344F, 0x3754, 0x38D5, 0x3A2B,
    0x3C78, 0x43AB, 0x44DB, 0x453D, 0x4772, 0x4AF4, 0x4DA6, 0x4E69, 0x519E, 0x5791, 0x58B3,
    0x59E8, 0x5B0D, 0x66AC, 0x691B, 0x6A87, 0x6DD0, 0x7127, 0x7259, 0x76C2, 0x85CD, 0x86A7,
    0x871B, 0x8B6A, 0x8DB1, 0x98DA, 0x9AE1, 0x9B94, 0xA176, 0xA9C3, 0xAC8E, 0xB0EC, 0xB11D,
    0xB433, 0xB689, 0xC24F, 0xC89D, 0xCE16, 0xD23A, 0xD364, 0xD455, 0xD54A,
};

/**
 * Hamming(7,4) codeword of a nibble, data bits first.
 * @param nibble Data
 * @return 7-bit codeword
 */
static uint8_t subghz_protocol_nero_radio_hamming(uint8_t nibble);
static void nero_radio_chip(NeroRadioChipWriter* writer, bool bit);
static void nero_radio_chip_gap(NeroRadioChipWriter* writer, uint32_t gap);
static void nero_radio_preamble(NeroRadioChipWriter* writer, uint8_t count, uint32_t last_low);
/**
 * Generating an upload for the rolling long range format.
 * @param instance Pointer to a SubGhzProtocolEncoderNeroRadio instance
 * @return true On success
 */
static bool
    subghz_protocol_encoder_nero_radio_get_upload_dyn(SubGhzProtocolEncoderNeroRadio* instance);
/**
 * Generating an upload for the normal rolling format.
 * @param instance Pointer to a SubGhzProtocolEncoderNeroRadio instance
 * @return true On success
 */
static bool
    subghz_protocol_encoder_nero_radio_get_upload_norm(SubGhzProtocolEncoderNeroRadio* instance);
/**
 * Generating an upload from data.
 * @param instance Pointer to a SubGhzProtocolEncoderNeroRadio instance
 * @return true On success
 */
static bool
    subghz_protocol_encoder_nero_radio_get_upload(SubGhzProtocolEncoderNeroRadio* instance);
/**
 * Deserialize the key, accepting 56/57 bit static and 66/72 bit rolling replay formats.
 * @param generic Pointer to a SubGhzBlockGeneric instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @return status
 */
static SubGhzProtocolStatus subghz_protocol_nero_radio_deserialize_key(
    SubGhzBlockGeneric* generic,
    FlipperFormat* flipper_format);

/**
 * Hamming(7,4) codeword of a nibble, data bits first.
 * @param nibble Data
 * @return 7-bit codeword
 */
static uint8_t subghz_protocol_nero_radio_hamming(uint8_t nibble) {
    static const uint8_t parity[4] = {0x7, 0x6, 0x5, 0x3}; // for data bits 0..3
    uint8_t par = 0;
    for(uint8_t i = 0; i < 4; i++) {
        if(nibble & (1 << i)) par ^= parity[i];
    }
    return (nibble << 3) | par;
}

static void nero_radio_chip(NeroRadioChipWriter* writer, bool bit) {
    if((bit != writer->level) && writer->run) {
        writer->upload[writer->index++] = level_duration_make(writer->level, writer->run);
        writer->run = 0;
    }
    writer->level = bit;
    writer->run += NERO_RADIO_DYN_TE;
}

static void nero_radio_chip_gap(NeroRadioChipWriter* writer, uint32_t gap) {
    if(writer->level) {
        writer->upload[writer->index++] = level_duration_make(true, writer->run);
        writer->run = 0;
    }
    writer->upload[writer->index++] = level_duration_make(false, writer->run + gap);
    writer->level = false;
    writer->run = 0;
}

static void nero_radio_preamble(NeroRadioChipWriter* writer, uint8_t count, uint32_t last_low) {
    for(uint8_t i = 0; i < count; i++) {
        writer->upload[writer->index++] = level_duration_make(true, NERO_RADIO_DYN_TE);
        writer->upload[writer->index++] =
            level_duration_make(false, (i == count - 1) ? last_low : NERO_RADIO_DYN_TE);
    }
}

/**
 * Generating an upload for the rolling long range format.
 * @param instance Pointer to a SubGhzProtocolEncoderNeroRadio instance
 * @return true On success
 */
static bool
    subghz_protocol_encoder_nero_radio_get_upload_dyn(SubGhzProtocolEncoderNeroRadio* instance) {
    // Carrier + preamble + sync + worst case one level per bit + gap
    size_t size_upload = 2 + NERO_RADIO_DYN_PREAMBLE * 2 + 2 + NERO_RADIO_DYN_FRAME_BITS + 1;
    if(size_upload > instance->encoder.size_upload) {
        FURI_LOG_E(TAG, "Size upload exceeds allocated encoder buffer.");
        return false;
    }
    NeroRadioChipWriter writer = {.upload = instance->encoder.upload};

    writer.upload[writer.index++] = level_duration_make(true, NERO_RADIO_DYN_CARRIER);
    writer.upload[writer.index++] = level_duration_make(false, 620);
    nero_radio_preamble(&writer, NERO_RADIO_DYN_PREAMBLE, NERO_RADIO_DYN_TE);
    writer.upload[writer.index++] = level_duration_make(true, 430);
    writer.upload[writer.index++] = level_duration_make(false, 1240);

    uint16_t words[NERO_RADIO_DYN_SYNC_WORDS + NERO_RADIO_DYN_DATA_WORDS];
    for(uint8_t i = 0; i < NERO_RADIO_DYN_SYNC_WORDS; i++) {
        words[i] = nero_radio_dyn_sync[i];
    }
    for(uint8_t i = 0; i < NERO_RADIO_DYN_DATA_WORDS - 1; i++) {
        words[NERO_RADIO_DYN_SYNC_WORDS + i] =
            nero_radio_dyn_symbols[(instance->generic.data >> ((9 - i) * 6)) & 0x3F];
    }
    words[NERO_RADIO_DYN_SYNC_WORDS + NERO_RADIO_DYN_DATA_WORDS - 1] =
        nero_radio_dyn_symbols[instance->generic.data_2 & 0x3F];

    for(uint16_t pos = 0; pos < NERO_RADIO_DYN_HEADER_BITS; pos++) {
        nero_radio_chip(
            &writer, (NERO_RADIO_DYN_HEADER >> (NERO_RADIO_DYN_HEADER_BITS - 1 - pos)) & 1);
    }
    for(uint16_t chip = 0; chip < COUNT_OF(words) * 16; chip++) {
        nero_radio_chip(&writer, (words[chip >> 4] >> (15 - (chip & 0xF))) & 1);
    }
    nero_radio_chip_gap(&writer, NERO_RADIO_DYN_GAP);

    instance->encoder.size_upload = writer.index;
    return true;
}

/**
 * Generating an upload for the normal rolling format.
 * @param instance Pointer to a SubGhzProtocolEncoderNeroRadio instance
 * @return true On success
 */
static bool
    subghz_protocol_encoder_nero_radio_get_upload_norm(SubGhzProtocolEncoderNeroRadio* instance) {
    // Carrier, id preamble + sync, id, preamble + start, worst case one level per chip, gap
    size_t size_upload = 2 + NERO_RADIO_NORM_ID_PREAM * 2 + 1 + NERO_RADIO_NORM_ID_BITS + 1 +
                         (NERO_RADIO_NORM_PREAMBLE + 1) * 2 + NERO_RADIO_DYN_FRAME_BITS + 1;
    if(size_upload > instance->encoder.size_upload) {
        FURI_LOG_E(TAG, "Size upload exceeds allocated encoder buffer.");
        return false;
    }
    NeroRadioChipWriter writer = {.upload = instance->encoder.upload};

    writer.upload[writer.index++] = level_duration_make(true, NERO_RADIO_DYN_CARRIER);
    writer.upload[writer.index++] = level_duration_make(false, 420);
    nero_radio_preamble(&writer, NERO_RADIO_NORM_ID_PREAM, 430);
    writer.upload[writer.index++] = level_duration_make(true, 1250);
    uint64_t id = instance->generic.data_2 >> 8;
    for(uint8_t i = 0; i < NERO_RADIO_NORM_ID_BITS; i++) {
        nero_radio_chip(&writer, (id >> (NERO_RADIO_NORM_ID_BITS - 1 - i)) & 1);
    }
    nero_radio_chip_gap(&writer, 1260);
    nero_radio_preamble(&writer, NERO_RADIO_NORM_PREAMBLE + 1, 830);

    for(uint8_t i = 0; i < NERO_RADIO_NORM_HEAD_BITS; i++) {
        nero_radio_chip(
            &writer, (NERO_RADIO_NORM_HEADER >> (NERO_RADIO_NORM_HEAD_BITS - 1 - i)) & 1);
    }
    for(uint8_t i = 0; i < 16; i++) {
        uint8_t word =
            subghz_protocol_nero_radio_hamming((instance->generic.data >> ((15 - i) * 4)) & 0xF);
        for(uint8_t j = 0; j < 7; j++) {
            bool bit = (word >> (6 - j)) & 1;
            nero_radio_chip(&writer, bit);
            nero_radio_chip(&writer, !bit);
        }
    }
    for(uint8_t i = 0; i < 2; i++) {
        uint8_t word =
            subghz_protocol_nero_radio_hamming((instance->generic.data_2 >> ((1 - i) * 4)) & 0xF);
        if(i) nero_radio_chip(&writer, false);
        for(uint8_t j = 0; j < 7; j++) {
            nero_radio_chip(&writer, (word >> (6 - j)) & 1);
        }
    }
    nero_radio_chip_gap(&writer, NERO_RADIO_DYN_GAP);

    instance->encoder.size_upload = writer.index;
    return true;
}

/**
 * Generating an upload from data.
 * @param instance Pointer to a SubGhzProtocolEncoderNeroRadio instance
 * @return true On success
 */
static bool
    subghz_protocol_encoder_nero_radio_get_upload(SubGhzProtocolEncoderNeroRadio* instance) {
    furi_assert(instance);
    if(instance->generic.data_count_bit == NERO_RADIO_DYN_COUNT_BIT) {
        return subghz_protocol_encoder_nero_radio_get_upload_dyn(instance);
    }
    if(instance->generic.data_count_bit == NERO_RADIO_NORM_COUNT_BIT) {
        return subghz_protocol_encoder_nero_radio_get_upload_norm(instance);
    }
    size_t index = 0;
    size_t size_upload = 49 * 2 + 2 + (instance->generic.data_count_bit * 2);
    if(size_upload > instance->encoder.size_upload) {
        FURI_LOG_E(TAG, "Size upload exceeds allocated encoder buffer.");
        return false;
    } else {
        instance->encoder.size_upload = size_upload;
    }

    //Send header
    for(uint8_t i = 0; i < 49; i++) {
        instance->encoder.upload[index++] =
            level_duration_make(true, (uint32_t)subghz_protocol_nero_radio_const.te_short);
        instance->encoder.upload[index++] =
            level_duration_make(false, (uint32_t)subghz_protocol_nero_radio_const.te_short);
    }

    //Send start bit
    instance->encoder.upload[index++] = level_duration_make(true, (uint32_t)830);
    instance->encoder.upload[index++] =
        level_duration_make(false, (uint32_t)subghz_protocol_nero_radio_const.te_short);

    //Send key data
    for(uint8_t i = instance->generic.data_count_bit; i > 1; i--) {
        if(bit_read(instance->generic.data, i - 1)) {
            //send bit 1
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_nero_radio_const.te_long);
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_nero_radio_const.te_short);
        } else {
            //send bit 0
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_nero_radio_const.te_short);
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_nero_radio_const.te_long);
        }
    }
    if(bit_read(instance->generic.data, 0)) {
        //send bit 1
        instance->encoder.upload[index++] =
            level_duration_make(true, (uint32_t)subghz_protocol_nero_radio_const.te_long);
        if(instance->generic.data_count_bit == 57) {
            instance->encoder.upload[index++] = level_duration_make(false, (uint32_t)1300);
        } else {
            instance->encoder.upload[index++] = level_duration_make(
                false, (uint32_t)subghz_protocol_nero_radio_const.te_short * 23);
        }
    } else {
        //send bit 0
        instance->encoder.upload[index++] =
            level_duration_make(true, (uint32_t)subghz_protocol_nero_radio_const.te_short);
        if(instance->generic.data_count_bit == 57) {
            instance->encoder.upload[index++] = level_duration_make(false, (uint32_t)1300);
        } else {
            instance->encoder.upload[index++] = level_duration_make(
                false, (uint32_t)subghz_protocol_nero_radio_const.te_short * 23);
        }
    }
    return true;
}

/**
 * Deserialize the key, accepting 56/57 bit static and 66/72 bit rolling replay formats.
 * @param generic Pointer to a SubGhzBlockGeneric instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @return status
 */
static SubGhzProtocolStatus subghz_protocol_nero_radio_deserialize_key(
    SubGhzBlockGeneric* generic,
    FlipperFormat* flipper_format) {
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        generic, flipper_format, subghz_protocol_nero_radio_const.min_count_bit_for_found);
    if(ret != SubGhzProtocolStatusErrorValueBitCount) {
        return ret;
    }
    if(generic->data_count_bit == 57) {
        return SubGhzProtocolStatusOk;
    }
    if((generic->data_count_bit != NERO_RADIO_DYN_COUNT_BIT) &&
       (generic->data_count_bit != NERO_RADIO_NORM_COUNT_BIT)) {
        return ret;
    }

    uint8_t key_data[sizeof(uint64_t)] = {0};
    if(!flipper_format_rewind(flipper_format) ||
       !flipper_format_read_hex(flipper_format, "Data", key_data, sizeof(uint64_t))) {
        FURI_LOG_E(TAG, "Missing Data");
        return SubGhzProtocolStatusErrorParserOthers;
    }
    generic->data_2 = 0;
    for(uint8_t i = 0; i < sizeof(uint64_t); i++) {
        generic->data_2 = generic->data_2 << 8 | key_data[i];
    }
    return SubGhzProtocolStatusOk;
}

/* vendored from lib/subghz/protocols/common.c (not exported) */
void subghz_protocol_encoder_common_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderCommon* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

/* vendored from lib/subghz/protocols/common.c (not exported) */
void subghz_protocol_encoder_common_stop(void* context) {
    SubGhzProtocolEncoderCommon* instance = context;
    instance->encoder.is_running = false;
}

/* vendored from lib/subghz/protocols/common.c (not exported) */
LevelDuration subghz_protocol_encoder_common_yield(void* context) {
    SubGhzProtocolEncoderCommon* instance = context;

    if(instance->encoder.repeat == 0 || !instance->encoder.is_running) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }

    LevelDuration ret = instance->encoder.upload[instance->encoder.front];

    if(++instance->encoder.front == instance->encoder.size_upload) {
        if(!subghz_block_generic_global.endless_tx) instance->encoder.repeat--;
        instance->encoder.front = 0;
    }

    return ret;
}

/* vendored from lib/subghz/protocols/common.c (not exported) */
void* subghz_protocol_encoder_common_alloc(
    size_t instance_size,
    const SubGhzProtocol* protocol,
    size_t repeat,
    size_t size_upload) {
    SubGhzProtocolEncoderCommonGeneric* instance = malloc(instance_size);
    instance->common.base.protocol = protocol;
    instance->generic.protocol_name = protocol->name;
    instance->common.encoder.repeat = repeat;
    instance->common.encoder.size_upload = size_upload;
    instance->common.encoder.upload = malloc(size_upload * sizeof(LevelDuration));
    instance->common.encoder.is_running = false;
    return instance;
}

void* subghz_protocol_encoder_nero_radio_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderNeroRadio),
        &subghz_protocol_nero_radio,
        3,
        NERO_RADIO_UPLOAD_CAPACITY);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_nero_radio_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderNeroRadio* instance = context;
    // A reload must stop the old frame and restore capacity after size_upload held its length.
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    instance->encoder.repeat = 3;
    instance->encoder.size_upload = NERO_RADIO_UPLOAD_CAPACITY;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_protocol_nero_radio_deserialize_key(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }
        // Optional value
        flipper_format_rewind(flipper_format);
        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        if(!subghz_protocol_encoder_nero_radio_get_upload(instance)) {
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }
        instance->encoder.is_running = true;
    } while(false);

    return ret;
}

static const SubGhzProtocolEncoder enc_nero_radio_vt = {
    .alloc = subghz_protocol_encoder_nero_radio_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_nero_radio_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_nero_radio = {
    .name = SUBGHZ_PROTOCOL_NERO_RADIO_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_nero_radio_vt,
};

static const SubGhzEncodePluginEntry enc_nero_radio_entry = {.name = SUBGHZ_PROTOCOL_NERO_RADIO_NAME, .encoder = &enc_nero_radio_vt};
static const FlipperAppPluginDescriptor enc_nero_radio_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_nero_radio_entry};
const FlipperAppPluginDescriptor* enc_nero_radio_ep(void) { return &enc_nero_radio_descriptor; }
