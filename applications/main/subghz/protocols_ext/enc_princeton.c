/* enc_princeton.fal : SD encoder for princeton (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_alloc enc_princeton__subghz_protocol_encoder_common_alloc
#define subghz_protocol_encoder_common_free enc_princeton__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_princeton__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_princeton__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_princeton_alloc enc_princeton__subghz_protocol_encoder_princeton_alloc
#define subghz_protocol_encoder_princeton_deserialize enc_princeton__subghz_protocol_encoder_princeton_deserialize
#define subghz_protocol_princeton enc_princeton__subghz_protocol_princeton
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/princeton.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolPrinceton"
#define PRINCETON_GUARD_TIME_DEFALUT 30

struct SubGhzProtocolEncoderPrinceton {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint32_t te;
    uint32_t guard_time;
};

static const SubGhzBlockConst subghz_protocol_princeton_const = {
    .te_short = 390,
    .te_long = 1170,
    .te_delta = 300,
    .min_count_bit_for_found = 24,
};

static uint8_t subghz_protocol_princeton_get_btn_code(void);
static bool
    subghz_protocol_encoder_princeton_get_upload(SubGhzProtocolEncoderPrinceton* instance);
static void subghz_protocol_princeton_check_remote_controller(SubGhzBlockGeneric* instance);

static uint8_t subghz_protocol_princeton_get_btn_code(void) {
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
            btn = 0x2;
            break;
        case 0x8:
            btn = 0x2;
            break;
        case 0xF:
            btn = 0x2;
            break;

        case 0x30:
            btn = 0xC0;
            break;
        case 0xC0:
            btn = 0x30;
            break;
        case 0xF3:
            btn = 0xC0;
            break;
        case 0xFC:
            btn = 0xC0;
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
            btn = 0x1;
            break;
        case 0x8:
            btn = 0x1;
            break;
        case 0xF:
            btn = 0x1;
            break;

        case 0x30:
            btn = 0xF3;
            break;
        case 0xC0:
            btn = 0xF3;
            break;
        case 0xF3:
            btn = 0x30;
            break;
        case 0xFC:
            btn = 0xF3;
            break;

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
            btn = 0x4;
            break;
        case 0xF:
            btn = 0x4;
            break;

        case 0x30:
            btn = 0xFC;
            break;
        case 0xC0:
            btn = 0xFC;
            break;
        case 0xF3:
            btn = 0xFC;
            break;
        case 0xFC:
            btn = 0x30;
            break;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_RIGHT) {
        switch(original_btn_code) {
        case 0x1:
            btn = 0xF;
            break;
        case 0x2:
            btn = 0xF;
            break;
        case 0x4:
            btn = 0xF;
            break;
        case 0x8:
            btn = 0xF;
            break;
        case 0xF:
            btn = 0x8;
            break;

        default:
            break;
        }
    }

    return btn;
}

static bool
    subghz_protocol_encoder_princeton_get_upload(SubGhzProtocolEncoderPrinceton* instance) {
    furi_assert(instance);

    instance->generic.btn = subghz_protocol_princeton_get_btn_code();

    if(subghz_block_generic_global_button_override_get(&instance->generic.btn))
        FURI_LOG_D(TAG, "Button sucessfully changed to 0x%X", instance->generic.btn);

    if(instance->generic.btn == 0x30 || instance->generic.btn == 0xC0) {
        instance->generic.data =
            ((uint64_t)instance->generic.serial << 8 | (uint64_t)instance->generic.btn);
    } else if(instance->generic.btn == 0xF3 || instance->generic.btn == 0xFC) {
        instance->generic.data =
            ((uint64_t)instance->generic.serial << 8 | (uint64_t)(instance->generic.btn & 0xF));
    } else {
        instance->generic.data =
            ((uint64_t)instance->generic.serial << 4 | (uint64_t)instance->generic.btn);
    }

    size_t index = 0;
    size_t size_upload = (instance->generic.data_count_bit * 2) + 2;
    if(size_upload > instance->encoder.size_upload) {
        FURI_LOG_E(TAG, "Size upload exceeds allocated encoder buffer.");
        return false;
    } else {
        instance->encoder.size_upload = size_upload;
    }

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if(bit_read(instance->generic.data, i - 1)) {

            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)instance->te * 3);
            instance->encoder.upload[index++] = level_duration_make(false, (uint32_t)instance->te);
        } else {

            instance->encoder.upload[index++] = level_duration_make(true, (uint32_t)instance->te);
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)instance->te * 3);
        }
    }

    instance->encoder.upload[index++] = level_duration_make(true, (uint32_t)instance->te);

    instance->encoder.upload[index++] =
        level_duration_make(false, (uint32_t)instance->te * instance->guard_time);

    return true;
}

static void subghz_protocol_princeton_check_remote_controller(SubGhzBlockGeneric* instance) {

    if((instance->data & 0xFF) == 0x30 || (instance->data & 0xFF) == 0xC0) {

        instance->serial = instance->data >> 8;
        instance->btn = instance->data & 0xFF;
    } else if((instance->data & 0xFF) == 0x03 || (instance->data & 0xFF) == 0x0C) {

        instance->serial = instance->data >> 8;
        instance->btn = (instance->data & 0xFF) | 0xF0;
    } else {
        instance->serial = instance->data >> 4;
        instance->btn = instance->data & 0xF;
    }

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

void* subghz_protocol_encoder_princeton_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderPrinceton), &subghz_protocol_princeton, 3, 52);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_princeton_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderPrinceton* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_princeton_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        if(!flipper_format_read_uint32(flipper_format, "TE", (uint32_t*)&instance->te, 1)) {
            FURI_LOG_E(TAG, "Missing TE");
            ret = SubGhzProtocolStatusErrorParserTe;
            break;
        }

        if(!flipper_format_read_uint32(
               flipper_format, "Guard_time", (uint32_t*)&instance->guard_time, 1)) {
            instance->guard_time = PRINCETON_GUARD_TIME_DEFALUT;
        } else {

            if((instance->guard_time < 15) || (instance->guard_time > 72)) {
                instance->guard_time = PRINCETON_GUARD_TIME_DEFALUT;
            }
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        subghz_protocol_princeton_check_remote_controller(&instance->generic);

        if(!subghz_protocol_encoder_princeton_get_upload(instance)) {
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }

        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }
        uint8_t key_data[sizeof(uint64_t)] = {0};
        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            key_data[sizeof(uint64_t) - i - 1] = (instance->generic.data >> i * 8) & 0xFF;
        }
        if(!flipper_format_update_hex(flipper_format, "Key", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Unable to add Key");
            break;
        }
        instance->encoder.is_running = true;
    } while(false);

    return ret;
}

static const SubGhzProtocolEncoder enc_princeton_vt = {
    .alloc = subghz_protocol_encoder_princeton_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_princeton_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_princeton = {
    .name = SUBGHZ_PROTOCOL_PRINCETON_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 | SubGhzProtocolFlag_315 |
            SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_Princeton,
    .decoder = NULL,
    .encoder = &enc_princeton_vt,
};

static const SubGhzEncodePluginEntry enc_princeton_entry = {.name = SUBGHZ_PROTOCOL_PRINCETON_NAME, .encoder = &enc_princeton_vt};
static const FlipperAppPluginDescriptor enc_princeton_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_princeton_entry};
const FlipperAppPluginDescriptor* enc_princeton_ep(void) { return &enc_princeton_descriptor; }
