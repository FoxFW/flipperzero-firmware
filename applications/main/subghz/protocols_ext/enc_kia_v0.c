/* enc_kia_v0.fal : SD encoder for kia_v0 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_free enc_kia_v0__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_kia_v0__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_kia_v0__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_kia_alloc enc_kia_v0__subghz_protocol_encoder_kia_alloc
#define subghz_protocol_encoder_kia_deserialize enc_kia_v0__subghz_protocol_encoder_kia_deserialize
#define subghz_protocol_kia_v0 enc_kia_v0__subghz_protocol_kia_v0
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/kia_v0.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolKiaV0"

struct SubGhzProtocolEncoderKIA {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_kia_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 100,
    .min_count_bit_for_found = 61,
};

static uint8_t kia_crc8(uint8_t* data, size_t len);
static uint8_t kia_calculate_crc(uint64_t data);
static bool kia_verify_crc(uint64_t data);
static bool subghz_protocol_encoder_kia_get_upload(SubGhzProtocolEncoderKIA* instance);
static void subghz_protocol_kia_check_remote_controller(SubGhzBlockGeneric* instance);

static uint8_t kia_crc8(uint8_t* data, size_t len) {
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

static uint8_t kia_calculate_crc(uint64_t data) {
    uint8_t crc_data[6];
    crc_data[0] = (data >> 48) & 0xFF;
    crc_data[1] = (data >> 40) & 0xFF;
    crc_data[2] = (data >> 32) & 0xFF;
    crc_data[3] = (data >> 24) & 0xFF;
    crc_data[4] = (data >> 16) & 0xFF;
    crc_data[5] = (data >> 8) & 0xFF;

    return kia_crc8(crc_data, 6);
}

static bool kia_verify_crc(uint64_t data) {
    uint8_t received_crc = data & 0xFF;
    uint8_t calculated_crc = kia_calculate_crc(data);
    return (received_crc == calculated_crc);
}

static bool subghz_protocol_encoder_kia_get_upload(SubGhzProtocolEncoderKIA* instance) {
    furi_assert(instance);

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->generic.btn);
    }
    subghz_custom_btn_set_max(4);

    size_t index = 0;
    size_t size_upload = (instance->generic.data_count_bit * 2 + 32) * 2 + 540;
    if(size_upload > instance->encoder.size_upload) {
        FURI_LOG_E(
            TAG,
            "Size upload exceeds allocated encoder buffer. %i",
            instance->generic.data_count_bit);
        return false;
    } else {
        instance->encoder.size_upload = size_upload;
    }

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

    uint8_t crc = kia_calculate_crc(data);
    data |= crc;

    instance->generic.data = data;

    for(uint16_t i = 270; i > 0; i--) {
        instance->encoder.upload[index++] =
            level_duration_make(true, (uint32_t)subghz_protocol_kia_const.te_short);
        instance->encoder.upload[index++] =
            level_duration_make(false, (uint32_t)subghz_protocol_kia_const.te_short);
    }

    for(uint8_t h = 2; h > 0; h--) {

        for(uint8_t i = 15; i > 0; i--) {
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_kia_const.te_short);
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_kia_const.te_short);
        }

        for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
            if(bit_read(instance->generic.data, i - 1)) {

                instance->encoder.upload[index++] =
                    level_duration_make(true, (uint32_t)subghz_protocol_kia_const.te_long);
                instance->encoder.upload[index++] =
                    level_duration_make(false, (uint32_t)subghz_protocol_kia_const.te_long);
            } else {

                instance->encoder.upload[index++] =
                    level_duration_make(true, (uint32_t)subghz_protocol_kia_const.te_short);
                instance->encoder.upload[index++] =
                    level_duration_make(false, (uint32_t)subghz_protocol_kia_const.te_short);
            }
        }

        instance->encoder.upload[index++] =
            level_duration_make(true, (uint32_t)subghz_protocol_kia_const.te_long * 3);
        instance->encoder.upload[index++] =
            level_duration_make(false, (uint32_t)subghz_protocol_kia_const.te_long * 3);
    }

    return true;
}

static void subghz_protocol_kia_check_remote_controller(SubGhzBlockGeneric* instance) {

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

void* subghz_protocol_encoder_kia_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderKIA* instance = malloc(sizeof(SubGhzProtocolEncoderKIA));
    instance->base.protocol = &subghz_protocol_kia_v0;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.size_upload = 848;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.repeat = 1;
    instance->encoder.is_running = false;

    return instance;
}

SubGhzProtocolStatus subghz_protocol_encoder_kia_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderKIA* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_kia_const.min_count_bit_for_found);

        if(ret != SubGhzProtocolStatusOk) {
            break;
        }

        subghz_protocol_kia_check_remote_controller(&instance->generic);

        if(!kia_verify_crc(instance->generic.data)) {
            FURI_LOG_W(TAG, "CRC mismatch in loaded file");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }

        if(!subghz_protocol_encoder_kia_get_upload(instance)) {
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }

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

static const SubGhzProtocolEncoder enc_kia_v0_vt = {
    .alloc = subghz_protocol_encoder_kia_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_kia_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_kia_v0 = {
    .name = SUBGHZ_PROTOCOL_KIA_V0_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_kia_v0_vt,
};

static const SubGhzEncodePluginEntry enc_kia_v0_entry = {.name = SUBGHZ_PROTOCOL_KIA_V0_NAME, .encoder = &enc_kia_v0_vt};
static const FlipperAppPluginDescriptor enc_kia_v0_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_kia_v0_entry};
const FlipperAppPluginDescriptor* enc_kia_v0_ep(void) { return &enc_kia_v0_descriptor; }
