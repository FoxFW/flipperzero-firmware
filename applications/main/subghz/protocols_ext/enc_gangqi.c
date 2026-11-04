/* enc_gangqi.fal : SD encoder for gangqi (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_alloc enc_gangqi__subghz_protocol_encoder_common_alloc
#define subghz_protocol_encoder_common_free enc_gangqi__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_gangqi__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_gangqi__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_gangqi_alloc enc_gangqi__subghz_protocol_encoder_gangqi_alloc
#define subghz_protocol_encoder_gangqi_deserialize enc_gangqi__subghz_protocol_encoder_gangqi_deserialize
#define subghz_protocol_gangqi enc_gangqi__subghz_protocol_gangqi
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/gangqi.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolGangQi"

struct SubGhzProtocolEncoderGangQi {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_gangqi_const = {
    .te_short = 500,
    .te_long = 1200,
    .te_delta = 200,
    .min_count_bit_for_found = 34,
};

static uint8_t subghz_protocol_gangqi_get_btn_code(void);
static void subghz_protocol_encoder_gangqi_get_upload(SubGhzProtocolEncoderGangQi* instance);
static void subghz_protocol_gangqi_remote_controller(SubGhzBlockGeneric* instance);

static uint8_t subghz_protocol_gangqi_get_btn_code(void) {
    uint8_t custom_btn_id = subghz_custom_btn_get();
    uint8_t original_btn_code = subghz_custom_btn_get_original();
    uint8_t btn = original_btn_code;

    if((custom_btn_id == SUBGHZ_CUSTOM_BTN_OK) && (original_btn_code != 0)) {

        btn = original_btn_code;
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_UP) {
        switch(original_btn_code) {
        case 0xD:
            btn = 0xE;
            break;
        case 0xE:
            btn = 0xD;
            break;
        case 0xB:
            btn = 0xD;
            break;
        case 0x7:
            btn = 0xD;
            break;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_DOWN) {
        switch(original_btn_code) {
        case 0xD:
            btn = 0xB;
            break;
        case 0xE:
            btn = 0xB;
            break;
        case 0xB:
            btn = 0xE;
            break;
        case 0x7:
            btn = 0xE;
            break;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_LEFT) {
        switch(original_btn_code) {
        case 0xD:
            btn = 0x7;
            break;
        case 0xE:
            btn = 0x7;
            break;
        case 0xB:
            btn = 0x7;
            break;
        case 0x7:
            btn = 0xB;
            break;

        default:
            break;
        }
    }

    return btn;
}

static void subghz_protocol_encoder_gangqi_get_upload(SubGhzProtocolEncoderGangQi* instance) {
    furi_assert(instance);

    instance->generic.btn = subghz_protocol_gangqi_get_btn_code();

    if(subghz_block_generic_global_button_override_get(&instance->generic.btn))
        FURI_LOG_D(TAG, "Button sucessfully changed to 0x%X", instance->generic.btn);

    uint16_t serial = (uint16_t)((instance->generic.data >> 18) & 0xFFFF);
    uint8_t const_and_button = (uint8_t)(0xD0 | instance->generic.btn);
    uint8_t serial_high = (uint8_t)(serial >> 8);
    uint8_t serial_low = (uint8_t)(serial & 0xFF);
    uint8_t bytesum = (uint8_t)(0xC8 - serial_high - serial_low - const_and_button);

    instance->generic.data = (instance->generic.data >> 14) << 14 | (instance->generic.btn << 10) |
                             (bytesum << 2);

    size_t index = 0;

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if(bit_read(instance->generic.data, i - 1)) {

            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_gangqi_const.te_long);
            if(i == 1) {

                instance->encoder.upload[index++] = level_duration_make(
                    false,
                    (uint32_t)subghz_protocol_gangqi_const.te_short * 4 +
                        subghz_protocol_gangqi_const.te_delta);
            } else {
                instance->encoder.upload[index++] =
                    level_duration_make(false, (uint32_t)subghz_protocol_gangqi_const.te_short);
            }
        } else {

            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_gangqi_const.te_short);
            if(i == 1) {

                instance->encoder.upload[index++] = level_duration_make(
                    false,
                    (uint32_t)subghz_protocol_gangqi_const.te_short * 4 +
                        subghz_protocol_gangqi_const.te_delta);
            } else {
                instance->encoder.upload[index++] =
                    level_duration_make(false, (uint32_t)subghz_protocol_gangqi_const.te_long);
            }
        }
    }

    instance->encoder.size_upload = index;
    return;
}

static void subghz_protocol_gangqi_remote_controller(SubGhzBlockGeneric* instance) {
    instance->btn = (instance->data >> 10) & 0xF;
    instance->serial = (instance->data & 0xFFFFF0000) >> 16;

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->btn);
    }
    subghz_custom_btn_set_max(3);

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

void* subghz_protocol_encoder_gangqi_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderGangQi), &subghz_protocol_gangqi, 3, 256);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_gangqi_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderGangQi* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_gangqi_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        subghz_protocol_gangqi_remote_controller(&instance->generic);
        subghz_protocol_encoder_gangqi_get_upload(instance);

        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }
        uint8_t key_data[sizeof(uint64_t)] = {0};
        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            key_data[sizeof(uint64_t) - i - 1] = (instance->generic.data >> (i * 8)) & 0xFF;
        }
        if(!flipper_format_update_hex(flipper_format, "Key", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Unable to add Key");
            break;
        }

        instance->encoder.is_running = true;
    } while(false);

    return ret;
}

static const SubGhzProtocolEncoder enc_gangqi_vt = {
    .alloc = subghz_protocol_encoder_gangqi_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_gangqi_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_gangqi = {
    .name = SUBGHZ_PROTOCOL_GANGQI_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send |
            SubGhzProtocolFlag_Alarms,
    .decoder = NULL,
    .encoder = &enc_gangqi_vt,
};

static const SubGhzEncodePluginEntry enc_gangqi_entry = {.name = SUBGHZ_PROTOCOL_GANGQI_NAME, .encoder = &enc_gangqi_vt};
static const FlipperAppPluginDescriptor enc_gangqi_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_gangqi_entry};
const FlipperAppPluginDescriptor* enc_gangqi_ep(void) { return &enc_gangqi_descriptor; }
