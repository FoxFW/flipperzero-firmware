/* enc_mitsubishi_v0a.fal : SD encoder for mitsubishi_v0a (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_free enc_mitsubishi_v0a__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_mitsubishi_v0a__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_mitsubishi_v0a__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_mitsubishi_v0a_alloc enc_mitsubishi_v0a__subghz_protocol_encoder_mitsubishi_v0a_alloc
#define subghz_protocol_encoder_mitsubishi_v0a_deserialize enc_mitsubishi_v0a__subghz_protocol_encoder_mitsubishi_v0a_deserialize
#define subghz_protocol_mitsubishi_v0a enc_mitsubishi_v0a__subghz_protocol_mitsubishi_v0a
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/mitsubishi_v0a.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/common.h>

#define TAG "MitsubishiProtocolV0a"
#define MITSUBISHI_V0A_BIT_COUNT 61U
#define MITSUBISHI_V0A_LONG_PREAMBLE_PAIRS 0x13FU
#define MITSUBISHI_V0A_SHORT_PREAMBLE_PAIRS 0x50U
#define MITSUBISHI_V0A_SYNC_US 750U
#define MITSUBISHI_V0A_GAP_US 1500U
#define MITSUBISHI_V0A_UPLOAD_CAPACITY                                          \
    (2U + (MITSUBISHI_V0A_LONG_PREAMBLE_PAIRS * 2U) + (MITSUBISHI_V0A_BIT_COUNT * 2U) + 2U + \
     (MITSUBISHI_V0A_SHORT_PREAMBLE_PAIRS * 2U) + (MITSUBISHI_V0A_BIT_COUNT * 2U) + 2U)

struct SubGhzProtocolEncoderMitsubishiV0a {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_mitsubishi_v0a_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 100,
    .min_count_bit_for_found = 61,
};

static uint8_t mitsubishi_v0a_crc8(uint8_t* data, size_t len);
static uint8_t mitsubishi_v0a_calculate_crc(uint64_t data);
static bool mitsubishi_v0a_verify_crc(uint64_t data);
static size_t
    mitsubishi_v0a_append_short_pairs(LevelDuration* upload, size_t index, uint32_t count);
static size_t mitsubishi_v0a_append_data_pairs(
    LevelDuration* upload,
    size_t index,
    uint64_t data,
    uint8_t bit_count);
static void mitsubishi_v0a_build_upload(SubGhzProtocolEncoderMitsubishiV0a* instance, uint64_t data);
static void subghz_protocol_mitsubishi_v0a_check_remote_controller(SubGhzBlockGeneric* instance);

static uint8_t mitsubishi_v0a_crc8(uint8_t* data, size_t len) {
    uint8_t crc = 0x00;
    for(size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for(size_t j = 0; j < 8; j++) {
            if((crc & 0x80) != 0)
                crc = (uint8_t)((crc << 1) ^ 0x7F);
            else
                crc <<= 1;
        }
    }
    return crc;
}

static uint8_t mitsubishi_v0a_calculate_crc(uint64_t data) {
    uint8_t crc_data[6];
    crc_data[0] = (data >> 48) & 0xFF;
    crc_data[1] = (data >> 40) & 0xFF;
    crc_data[2] = (data >> 32) & 0xFF;
    crc_data[3] = (data >> 24) & 0xFF;
    crc_data[4] = (data >> 16) & 0xFF;
    crc_data[5] = (data >> 8) & 0xFF;

    return mitsubishi_v0a_crc8(crc_data, 6);
}

static bool mitsubishi_v0a_verify_crc(uint64_t data) {
    uint8_t received_crc = data & 0xFF;
    uint8_t calculated_crc = mitsubishi_v0a_calculate_crc(data);
    return (received_crc == calculated_crc);
}

static size_t
    mitsubishi_v0a_append_short_pairs(LevelDuration* upload, size_t index, uint32_t count) {
    for(uint32_t i = count; i > 0; i--) {
        upload[index++] =
            level_duration_make(true, (uint32_t)subghz_protocol_mitsubishi_v0a_const.te_short);
        upload[index++] =
            level_duration_make(false, (uint32_t)subghz_protocol_mitsubishi_v0a_const.te_short);
    }
    return index;
}

static size_t mitsubishi_v0a_append_data_pairs(
    LevelDuration* upload,
    size_t index,
    uint64_t data,
    uint8_t bit_count) {
    for(uint8_t i = bit_count; i > 0; i--) {
        uint32_t duration = bit_read(data, i - 1) ?
                                 (uint32_t)subghz_protocol_mitsubishi_v0a_const.te_long :
                                 (uint32_t)subghz_protocol_mitsubishi_v0a_const.te_short;
        upload[index++] = level_duration_make(true, duration);
        upload[index++] = level_duration_make(false, duration);
    }
    return index;
}

static void mitsubishi_v0a_build_upload(SubGhzProtocolEncoderMitsubishiV0a* instance, uint64_t data) {
    size_t index = 0;
    instance->encoder.upload[index++] = level_duration_make(true, MITSUBISHI_V0A_SYNC_US);
    instance->encoder.upload[index++] = level_duration_make(false, MITSUBISHI_V0A_SYNC_US);
    index = mitsubishi_v0a_append_short_pairs(
        instance->encoder.upload, index, MITSUBISHI_V0A_LONG_PREAMBLE_PAIRS);
    index = mitsubishi_v0a_append_data_pairs(
        instance->encoder.upload, index, data, MITSUBISHI_V0A_BIT_COUNT);

    instance->encoder.upload[index++] = level_duration_make(true, MITSUBISHI_V0A_GAP_US);
    instance->encoder.upload[index++] = level_duration_make(false, MITSUBISHI_V0A_GAP_US);
    index = mitsubishi_v0a_append_short_pairs(
        instance->encoder.upload, index, MITSUBISHI_V0A_SHORT_PREAMBLE_PAIRS);
    index = mitsubishi_v0a_append_data_pairs(
        instance->encoder.upload, index, data, MITSUBISHI_V0A_BIT_COUNT);

    instance->encoder.upload[index++] = level_duration_make(true, MITSUBISHI_V0A_GAP_US);
    instance->encoder.upload[index++] = level_duration_make(false, MITSUBISHI_V0A_GAP_US);

    instance->encoder.front = 0;
    instance->encoder.size_upload = index;
}

static void subghz_protocol_mitsubishi_v0a_check_remote_controller(SubGhzBlockGeneric* instance) {

    instance->serial = (uint32_t)((instance->data >> 12) & 0x0FFFFFFF);
    instance->btn = (instance->data >> 8) & 0x0F;
    instance->cnt = (instance->data >> 40) & 0xFFFF;

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->btn);
    }

    subghz_custom_btn_set_max(4);
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

void* subghz_protocol_encoder_mitsubishi_v0a_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderMitsubishiV0a* instance =
        malloc(sizeof(SubGhzProtocolEncoderMitsubishiV0a));
    instance->base.protocol = &subghz_protocol_mitsubishi_v0a;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.size_upload = MITSUBISHI_V0A_UPLOAD_CAPACITY;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.repeat = 1;
    instance->encoder.is_running = false;

    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_mitsubishi_v0a_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderMitsubishiV0a* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_mitsubishi_v0a_const.min_count_bit_for_found);

        if(ret != SubGhzProtocolStatusOk) {
            break;
        }

        subghz_protocol_mitsubishi_v0a_check_remote_controller(&instance->generic);

        if(!mitsubishi_v0a_verify_crc(instance->generic.data)) {
            FURI_LOG_W(TAG, "CRC mismatch in loaded file");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }

        if(subghz_custom_btn_get_original() == 0) {
            subghz_custom_btn_set_original(instance->generic.btn);
        }
        subghz_custom_btn_set_max(4);

        if(instance->generic.cnt < 0xFFFF) {
            if((instance->generic.cnt + furi_hal_subghz_get_rolling_counter_mult()) > 0xFFFF) {
                instance->generic.cnt = 0;
            } else {
                instance->generic.cnt += furi_hal_subghz_get_rolling_counter_mult();
            }
        } else if(instance->generic.cnt >= 0xFFFF) {
            instance->generic.cnt = 0;
        }

        uint8_t btn = subghz_custom_btn_get() == SUBGHZ_CUSTOM_BTN_OK ?
                          subghz_custom_btn_get_original() :
                          subghz_custom_btn_get();
        instance->generic.btn = btn;

        uint64_t data = 0;
        data |= ((uint64_t)(0x0F) << 56);
        data |= ((uint64_t)(instance->generic.cnt & 0xFFFF) << 40);
        data |= ((uint64_t)(instance->generic.serial & 0x0FFFFFFF) << 12);
        data |= ((uint64_t)(btn & 0x0F) << 8);
        data |= mitsubishi_v0a_calculate_crc(data);
        instance->generic.data = data;

        mitsubishi_v0a_build_upload(instance, data);

        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        uint8_t key_data[sizeof(uint64_t)] = {0};
        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            key_data[sizeof(uint64_t) - i - 1] = (instance->generic.data >> i * 8) & 0xFF;
        }
        if(!flipper_format_update_hex(flipper_format, "Key", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Unable to update Key");
            ret = SubGhzProtocolStatusErrorParserKey;
            break;
        }

        instance->encoder.is_running = true;
    } while(false);

    return ret;
}

static const SubGhzProtocolEncoder enc_mitsubishi_v0a_vt = {
    .alloc = subghz_protocol_encoder_mitsubishi_v0a_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_mitsubishi_v0a_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_mitsubishi_v0a = {
    .name = MITSUBISHI_PROTOCOL_V0A_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_mitsubishi_v0a_vt,
};

static const SubGhzEncodePluginEntry enc_mitsubishi_v0a_entry = {.name = MITSUBISHI_PROTOCOL_V0A_NAME, .encoder = &enc_mitsubishi_v0a_vt};
static const FlipperAppPluginDescriptor enc_mitsubishi_v0a_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_mitsubishi_v0a_entry};
const FlipperAppPluginDescriptor* enc_mitsubishi_v0a_ep(void) { return &enc_mitsubishi_v0a_descriptor; }
