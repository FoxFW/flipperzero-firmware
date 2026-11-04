/* enc_hollarm.fal : SD encoder for hollarm (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_alloc enc_hollarm__subghz_protocol_encoder_common_alloc
#define subghz_protocol_encoder_common_free enc_hollarm__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_hollarm__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_hollarm__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_hollarm_alloc enc_hollarm__subghz_protocol_encoder_hollarm_alloc
#define subghz_protocol_encoder_hollarm_deserialize enc_hollarm__subghz_protocol_encoder_hollarm_deserialize
#define subghz_protocol_hollarm enc_hollarm__subghz_protocol_hollarm
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/hollarm.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolHollarm"

struct SubGhzProtocolEncoderHollarm {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_hollarm_const = {
    .te_short = 200,
    .te_long = 1000,
    .te_delta = 200,
    .min_count_bit_for_found = 42,
};

static uint8_t subghz_protocol_hollarm_get_btn_code(void);
static void subghz_protocol_encoder_hollarm_get_upload(SubGhzProtocolEncoderHollarm* instance);
static void subghz_protocol_hollarm_remote_controller(SubGhzBlockGeneric* instance);

static uint8_t subghz_protocol_hollarm_get_btn_code(void) {
    uint8_t custom_btn_id = subghz_custom_btn_get();
    uint8_t original_btn_code = subghz_custom_btn_get_original();
    uint8_t btn = original_btn_code;

    if((custom_btn_id == SUBGHZ_CUSTOM_BTN_OK) && (original_btn_code != 0)) {

        btn = original_btn_code;
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_UP) {
        switch(original_btn_code) {
        case 0x1:
            btn = 0x2;
            break;
        case 0x2:
            btn = 0x1;
            break;
        case 0x4:
            btn = 0x1;
            break;
        case 0x8:
            btn = 0x1;
            break;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_DOWN) {
        switch(original_btn_code) {
        case 0x1:
            btn = 0x4;
            break;
        case 0x2:
            btn = 0x4;
            break;
        case 0x4:
            btn = 0x2;
            break;
        case 0x8:
            btn = 0x4;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_LEFT) {
        switch(original_btn_code) {
        case 0x1:
            btn = 0x8;
            break;
        case 0x2:
            btn = 0x8;
            break;
        case 0x4:
            btn = 0x8;
            break;
        case 0x8:
            btn = 0x2;
            break;

        default:
            break;
        }
    }

    return btn;
}

static void subghz_protocol_encoder_hollarm_get_upload(SubGhzProtocolEncoderHollarm* instance) {
    furi_assert(instance);

    instance->generic.btn = subghz_protocol_hollarm_get_btn_code();

    if(subghz_block_generic_global_button_override_get(&instance->generic.btn))
        FURI_LOG_D(TAG, "Button sucessfully changed to 0x%X", instance->generic.btn);

    uint64_t new_key = (instance->generic.data >> 12) << 12 | (instance->generic.btn << 8);

    uint8_t bytesum = ((new_key >> 32) & 0xFF) + ((new_key >> 24) & 0xFF) +
                      ((new_key >> 16) & 0xFF) + ((new_key >> 8) & 0xFF);

    instance->generic.data = (new_key | bytesum);

    size_t index = 0;

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {

        if(bit_read((instance->generic.data << 2), i - 1)) {

            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_hollarm_const.te_short);
            if(i == 1) {

                instance->encoder.upload[index++] = level_duration_make(
                    false, (uint32_t)subghz_protocol_hollarm_const.te_short * 12);
            } else {
                instance->encoder.upload[index++] = level_duration_make(
                    false, (uint32_t)subghz_protocol_hollarm_const.te_short * 8);
            }
        } else {

            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_hollarm_const.te_short);
            if(i == 1) {

                instance->encoder.upload[index++] = level_duration_make(
                    false, (uint32_t)subghz_protocol_hollarm_const.te_short * 12);
            } else {
                instance->encoder.upload[index++] =
                    level_duration_make(false, (uint32_t)subghz_protocol_hollarm_const.te_long);
            }
        }
    }

    instance->encoder.size_upload = index;
    return;
}

static void subghz_protocol_hollarm_remote_controller(SubGhzBlockGeneric* instance) {
    instance->btn = (instance->data >> 8) & 0xF;
    instance->serial = (instance->data & 0xFFFFFFF0000) >> 16;

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

void* subghz_protocol_encoder_hollarm_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderHollarm), &subghz_protocol_hollarm, 3, 128);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_hollarm_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderHollarm* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_hollarm_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        subghz_protocol_hollarm_remote_controller(&instance->generic);
        subghz_protocol_encoder_hollarm_get_upload(instance);

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

static const SubGhzProtocolEncoder enc_hollarm_vt = {
    .alloc = subghz_protocol_encoder_hollarm_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_hollarm_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_hollarm = {
    .name = SUBGHZ_PROTOCOL_HOLLARM_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send |
            SubGhzProtocolFlag_Alarms,
    .decoder = NULL,
    .encoder = &enc_hollarm_vt,
};

static const SubGhzEncodePluginEntry enc_hollarm_entry = {.name = SUBGHZ_PROTOCOL_HOLLARM_NAME, .encoder = &enc_hollarm_vt};
static const FlipperAppPluginDescriptor enc_hollarm_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_hollarm_entry};
const FlipperAppPluginDescriptor* enc_hollarm_ep(void) { return &enc_hollarm_descriptor; }
