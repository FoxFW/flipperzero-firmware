/* enc_phoenix_v2.fal : SD encoder for phoenix_v2 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_alloc enc_phoenix_v2__subghz_protocol_encoder_common_alloc
#define subghz_protocol_encoder_common_free enc_phoenix_v2__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_phoenix_v2__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_phoenix_v2__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_phoenix_v2_alloc enc_phoenix_v2__subghz_protocol_encoder_phoenix_v2_alloc
#define subghz_protocol_encoder_phoenix_v2_deserialize enc_phoenix_v2__subghz_protocol_encoder_phoenix_v2_deserialize
#define subghz_protocol_phoenix_v2 enc_phoenix_v2__subghz_protocol_phoenix_v2
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/phoenix_v2.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolPhoenixV2"

struct SubGhzProtocolEncoderPhoenix_V2 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_phoenix_v2_const = {
    .te_short = 427,
    .te_long = 853,
    .te_delta = 100,
    .min_count_bit_for_found = 52,
};

/* host bridge: live firmware functions/state this encoder needs (set by the loader) */
extern bool bypass__bridge_ty;
extern bool phoenix_v2_raw_replay__bridge_ty;
extern uint8_t v2_phoenix_counter_mode__bridge_ty;
typedef struct {
    void* bypass;
    void* phoenix_v2_raw_replay;
    void* v2_phoenix_counter_mode;
} enc_phoenix_v2_host_t;
static const void* enc_phoenix_v2_host_raw;
#define ENC_PHOENIX_V2_HOST ((const enc_phoenix_v2_host_t*)enc_phoenix_v2_host_raw)
#define bypass (*(__typeof__(bypass__bridge_ty)*)(ENC_PHOENIX_V2_HOST->bypass))
#define phoenix_v2_raw_replay (*(__typeof__(phoenix_v2_raw_replay__bridge_ty)*)(ENC_PHOENIX_V2_HOST->phoenix_v2_raw_replay))
#define v2_phoenix_counter_mode (*(__typeof__(v2_phoenix_counter_mode__bridge_ty)*)(ENC_PHOENIX_V2_HOST->v2_phoenix_counter_mode))

static uint8_t subghz_protocol_phoenix_v2_get_btn_code(void);
static bool
    subghz_protocol_encoder_phoenix_v2_get_upload(SubGhzProtocolEncoderPhoenix_V2* instance);
static uint16_t subghz_protocol_phoenix_v2_encrypt_counter(uint64_t full_key, uint16_t counter);
static uint16_t subghz_protocol_phoenix_v2_decrypt_counter(uint64_t full_key);
static void subghz_protocol_phoenix_v2_check_remote_controller(SubGhzBlockGeneric* instance);

static uint8_t subghz_protocol_phoenix_v2_get_btn_code(void) {
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
        case 0x3:
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
            break;
        case 0x3:
            btn = 0x4;
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
            btn = 0x2;
            break;
        case 0x3:
            btn = 0x8;
            break;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_RIGHT) {
        switch(original_btn_code) {
        case 0x1:
            btn = 0x3;
            break;
        case 0x2:
            btn = 0x3;
            break;
        case 0x4:
            btn = 0x3;
            break;
        case 0x8:
            btn = 0x3;
            break;
        case 0x3:
            btn = 0x2;
            break;

        default:
            break;
        }
    }

    return btn;
}

static bool
    subghz_protocol_encoder_phoenix_v2_get_upload(SubGhzProtocolEncoderPhoenix_V2* instance) {
    furi_assert(instance);
    size_t index = 0;
    size_t size_upload = (instance->generic.data_count_bit * 2) + 2;
    if(size_upload > instance->encoder.size_upload) {
        FURI_LOG_E(TAG, "Size upload exceeds allocated encoder buffer.");
        return false;
    } else {
        instance->encoder.size_upload = size_upload;
    }

    uint8_t btn = instance->generic.btn;

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(btn);
    }

    btn = subghz_protocol_phoenix_v2_get_btn_code();

    if(subghz_block_generic_global_button_override_get(&btn)) {
        bypass = true;
        FURI_LOG_D(TAG, "Button sucessfully changed to 0x%X", btn);
    }

    if(subghz_block_generic_global.cnt_need_override) bypass = true;

    if(v2_phoenix_counter_mode == 0 || bypass) {

        if(phoenix_v2_raw_replay) {
            bypass = false; /* #16: freeze counter on TxRaw verbatim replay */
        } else if(furi_hal_subghz_get_rolling_counter_mult() != -0x7FFFFFFF || bypass) {
            bypass = false;

            if(!subghz_block_generic_global_counter_override_get(&instance->generic.cnt)) {

                if((instance->generic.cnt + furi_hal_subghz_get_rolling_counter_mult()) > 0xFFFF) {
                    instance->generic.cnt = 0;
                } else {
                    instance->generic.cnt += furi_hal_subghz_get_rolling_counter_mult();
                }
            }
        } else {
            if((instance->generic.cnt + 0x1) > 0xFFFF) {
                instance->generic.cnt = 0;
            } else if(instance->generic.cnt >= 0x1 && instance->generic.cnt != 0xFFFE) {
                instance->generic.cnt = 0xFFFE;
            } else {
                instance->generic.cnt++;
            }
        }
    } else if(v2_phoenix_counter_mode == 1) {

        if((instance->generic.cnt + 0x1) > 0xFFFF) {
            instance->generic.cnt = 0;
        } else if(instance->generic.cnt >= 0x1 && instance->generic.cnt != 0xFFFE) {
            instance->generic.cnt = 0xFFFE;
        } else {
            instance->generic.cnt++;
        }
    } else {

        if(instance->generic.cnt >= 0x0004) {
            instance->generic.cnt = 0;
        } else {
            instance->generic.cnt++;
        }
    }

    uint64_t local_data_rev = subghz_protocol_blocks_reverse_key(
        instance->generic.data, instance->generic.data_count_bit + 4);

    uint16_t encrypted_counter = (uint16_t)subghz_protocol_phoenix_v2_encrypt_counter(
        local_data_rev, instance->generic.cnt);

    instance->generic.data = subghz_protocol_blocks_reverse_key(
        (uint64_t)(((uint64_t)encrypted_counter << 40) | ((uint64_t)btn << 32) |
                   (uint64_t)instance->generic.serial),
        instance->generic.data_count_bit + 4);

    instance->encoder.upload[index++] =
        level_duration_make(false, (uint32_t)subghz_protocol_phoenix_v2_const.te_short * 60);

    instance->encoder.upload[index++] =
        level_duration_make(true, (uint32_t)subghz_protocol_phoenix_v2_const.te_short * 6);

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if(!bit_read(instance->generic.data, i - 1)) {

            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_phoenix_v2_const.te_long);
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_phoenix_v2_const.te_short);
        } else {

            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_phoenix_v2_const.te_short);
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_phoenix_v2_const.te_long);
        }
    }
    return true;
}

static uint16_t subghz_protocol_phoenix_v2_encrypt_counter(uint64_t full_key, uint16_t counter) {
    uint8_t xor_key1 = (uint8_t)(full_key >> 24);
    uint8_t xor_key2 = (uint8_t)((full_key >> 16) & 0xFF);

    uint8_t byte2 = (uint8_t)(counter >> 8);
    uint8_t byte1 = (uint8_t)(counter & 0xFF);

    for(int i = 0; i < 16; i++) {

        uint8_t msb_of_prev_byte1 = byte2 & 0x80;

        if(msb_of_prev_byte1 == 0) {

            byte2 ^= xor_key2;
            byte1 ^= xor_key1;
        }

        uint8_t lsb_of_current_byte1 = byte1 & 1;

        byte2 = (byte2 << 1) | lsb_of_current_byte1;
        byte1 = (byte1 >> 1) | msb_of_prev_byte1;
    }

    return (uint16_t)byte1 << 8 | byte2;
}

static uint16_t subghz_protocol_phoenix_v2_decrypt_counter(uint64_t full_key) {
    uint16_t encrypted_value = (uint16_t)((full_key >> 40) & 0xFFFF);

    uint8_t byte1 = (uint8_t)(encrypted_value >> 8);
    uint8_t byte2 = (uint8_t)(encrypted_value & 0xFF);

    uint8_t xor_key1 = (uint8_t)(full_key >> 24);
    uint8_t xor_key2 = (uint8_t)((full_key >> 16) & 0xFF);

    for(int i = 0; i < 16; i++) {

        uint8_t msb_of_byte1 = byte1 & 0x80;

        uint8_t lsb_of_byte2 = byte2 & 1;

        byte2 = (byte2 >> 1) | msb_of_byte1;
        byte1 = (byte1 << 1) | lsb_of_byte2;

        if(msb_of_byte1 == 0) {
            byte1 ^= xor_key1;

            byte2 = (byte2 ^ xor_key2) & 0x7F;
        }
    }

    return (uint16_t)byte2 << 8 | byte1;
}

static void subghz_protocol_phoenix_v2_check_remote_controller(SubGhzBlockGeneric* instance) {

    uint64_t data_rev =
        subghz_protocol_blocks_reverse_key(instance->data, instance->data_count_bit + 4);

    instance->serial = data_rev & 0xFFFFFFFF;
    instance->cnt = subghz_protocol_phoenix_v2_decrypt_counter(data_rev);
    instance->btn = (data_rev >> 32) & 0xF;

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

void* subghz_protocol_encoder_phoenix_v2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderPhoenix_V2), &subghz_protocol_phoenix_v2, 3, 128);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_phoenix_v2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderPhoenix_V2* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        /* #16: TxRaw verbatim replay — freeze the rolling counter. */
        phoenix_v2_raw_replay = false;
        {
            uint32_t phoenix_v2_txraw = 0;
            flipper_format_rewind(flipper_format);
            if(flipper_format_read_uint32(flipper_format, "TxRaw", &phoenix_v2_txraw, 1) &&
               phoenix_v2_txraw != 0) {
                phoenix_v2_raw_replay = true;
            }
            flipper_format_rewind(flipper_format);
        }
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_phoenix_v2_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }

        uint32_t tmp_counter_mode;
        if(flipper_format_read_uint32(flipper_format, "CounterMode", &tmp_counter_mode, 1)) {
            v2_phoenix_counter_mode = (uint8_t)tmp_counter_mode;
        } else {
            v2_phoenix_counter_mode = 0;
        }

        subghz_protocol_phoenix_v2_check_remote_controller(&instance->generic);

        if(!subghz_protocol_encoder_phoenix_v2_get_upload(instance)) {
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

static const SubGhzProtocolEncoder enc_phoenix_v2_vt = {
    .alloc = subghz_protocol_encoder_phoenix_v2_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_phoenix_v2_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_phoenix_v2 = {
    .name = SUBGHZ_PROTOCOL_PHOENIX_V2_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = NULL,
    .encoder = &enc_phoenix_v2_vt,
};

static const SubGhzEncodePluginEntry enc_phoenix_v2_entry = {.name = SUBGHZ_PROTOCOL_PHOENIX_V2_NAME, .encoder = &enc_phoenix_v2_vt, .host_slot = &enc_phoenix_v2_host_raw};
static const FlipperAppPluginDescriptor enc_phoenix_v2_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_phoenix_v2_entry};
const FlipperAppPluginDescriptor* enc_phoenix_v2_ep(void) { return &enc_phoenix_v2_descriptor; }
