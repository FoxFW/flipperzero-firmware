/* enc_somfy_telis.fal : SD encoder for somfy_telis (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_alloc enc_somfy_telis__subghz_protocol_encoder_common_alloc
#define subghz_protocol_encoder_common_free enc_somfy_telis__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_somfy_telis__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_somfy_telis__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_somfy_telis_alloc enc_somfy_telis__subghz_protocol_encoder_somfy_telis_alloc
#define subghz_protocol_encoder_somfy_telis_deserialize enc_somfy_telis__subghz_protocol_encoder_somfy_telis_deserialize
#define subghz_protocol_somfy_telis enc_somfy_telis__subghz_protocol_somfy_telis
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/somfy_telis.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolSomfyTelis"

struct SubGhzProtocolEncoderSomfyTelis {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_somfy_telis_const = {
    .te_short = 640,
    .te_long = 1280,
    .te_delta = 250,
    .min_count_bit_for_found = 56,
};

/* host bridge: live firmware functions/state this encoder needs (set by the loader) */
extern bool somfy_telis_raw_replay__bridge_ty;
typedef struct {
    void* somfy_telis_raw_replay;
} enc_somfy_telis_host_t;
static const void* enc_somfy_telis_host_raw;
#define ENC_SOMFY_TELIS_HOST ((const enc_somfy_telis_host_t*)enc_somfy_telis_host_raw)
#define somfy_telis_raw_replay (*(__typeof__(somfy_telis_raw_replay__bridge_ty)*)(ENC_SOMFY_TELIS_HOST->somfy_telis_raw_replay))

static bool subghz_protocol_somfy_telis_gen_data(
    SubGhzProtocolEncoderSomfyTelis* instance,
    uint8_t btn,
    bool new_remote);
static bool subghz_protocol_encoder_somfy_telis_get_upload(
    SubGhzProtocolEncoderSomfyTelis* instance,
    uint8_t btn);
static uint8_t subghz_protocol_somfy_telis_get_btn_code(void);

static bool subghz_protocol_somfy_telis_gen_data(
    SubGhzProtocolEncoderSomfyTelis* instance,
    uint8_t btn,
    bool new_remote) {

    uint64_t data = instance->generic.data ^ (instance->generic.data >> 8);
    if(!new_remote) {
        instance->generic.btn = (data >> 44) & 0xF;
        btn = instance->generic.btn;
        instance->generic.cnt = (data >> 24) & 0xFFFF;
        instance->generic.serial = data & 0xFFFFFF;
    }

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(btn);
    }

    btn = subghz_protocol_somfy_telis_get_btn_code();

    if(subghz_block_generic_global_button_override_get(&btn))
        FURI_LOG_D(TAG, "Button sucessfully changed to 0x%X", btn);

    if(somfy_telis_raw_replay) {
        /* #16: freeze counter on TxRaw verbatim replay */
    } else if(furi_hal_subghz_get_rolling_counter_mult() != -0x7FFFFFFF) {

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

    uint8_t frame[7];
    if(!new_remote) {
        frame[0] = data >> 48;
    } else {
        frame[0] = 0xA7;
    }
    frame[1] = btn << 4;
    frame[2] = instance->generic.cnt >> 8;
    frame[3] = instance->generic.cnt;
    frame[4] = instance->generic.serial >> 16;
    frame[5] = instance->generic.serial >> 8;
    frame[6] = instance->generic.serial;

    uint8_t checksum = 0;
    for(uint8_t i = 0; i < 7; i++) {
        checksum = checksum ^ frame[i] ^ (frame[i] >> 4);
    }
    checksum &= 0xF;

    frame[1] |= checksum;

    for(uint8_t i = 1; i < 7; i++) {
        frame[i] ^= frame[i - 1];
    }
    data = 0;
    for(uint8_t i = 0; i < 7; ++i) {
        data <<= 8;
        data |= frame[i];
    }
    instance->generic.data = data;
    return true;
}

static bool subghz_protocol_encoder_somfy_telis_get_upload(
    SubGhzProtocolEncoderSomfyTelis* instance,
    uint8_t btn) {
    furi_assert(instance);

    if(!subghz_protocol_somfy_telis_gen_data(instance, btn, false)) {
        return false;
    }

    size_t index = 0;

    instance->encoder.upload[index++] = level_duration_make(true, (uint32_t)9415);
    instance->encoder.upload[index++] = level_duration_make(false, (uint32_t)89565);

    for(uint8_t i = 0; i < 2; ++i) {
        instance->encoder.upload[index++] = level_duration_make(
            true, (uint32_t)subghz_protocol_somfy_telis_const.te_short * 4);
        instance->encoder.upload[index++] = level_duration_make(
            false, (uint32_t)subghz_protocol_somfy_telis_const.te_short * 4);
    }

    instance->encoder.upload[index++] = level_duration_make(true, (uint32_t)4550);
    instance->encoder.upload[index++] =
        level_duration_make(false, (uint32_t)subghz_protocol_somfy_telis_const.te_short);

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if(bit_read(instance->generic.data, i - 1)) {
            if(instance->encoder.upload[index - 1].level == LEVEL_DURATION_LEVEL_LOW) {
                instance->encoder.upload[index - 1].duration *= 2;
                instance->encoder.upload[index++] = level_duration_make(
                    true, (uint32_t)subghz_protocol_somfy_telis_const.te_short);
            } else {
                instance->encoder.upload[index++] = level_duration_make(
                    false, (uint32_t)subghz_protocol_somfy_telis_const.te_short);
                instance->encoder.upload[index++] = level_duration_make(
                    true, (uint32_t)subghz_protocol_somfy_telis_const.te_short);
            }

        } else {
            if(instance->encoder.upload[index - 1].level == LEVEL_DURATION_LEVEL_HIGH) {
                instance->encoder.upload[index - 1].duration *= 2;
                instance->encoder.upload[index++] = level_duration_make(
                    false, (uint32_t)subghz_protocol_somfy_telis_const.te_short);
            } else {
                instance->encoder.upload[index++] = level_duration_make(
                    true, (uint32_t)subghz_protocol_somfy_telis_const.te_short);
                instance->encoder.upload[index++] = level_duration_make(
                    false, (uint32_t)subghz_protocol_somfy_telis_const.te_short);
            }
        }
    }

    if(instance->encoder.upload[index - 1].level == LEVEL_DURATION_LEVEL_LOW) {
        instance->encoder.upload[index - 1].duration += (uint32_t)30415;
    } else {
        instance->encoder.upload[index++] = level_duration_make(false, (uint32_t)30415);
    }

    for(uint8_t i = 0; i < 2; i++) {

        for(uint8_t i = 0; i < 7; ++i) {
            instance->encoder.upload[index++] = level_duration_make(
                true, (uint32_t)subghz_protocol_somfy_telis_const.te_short * 4);
            instance->encoder.upload[index++] = level_duration_make(
                false, (uint32_t)subghz_protocol_somfy_telis_const.te_short * 4);
        }

        instance->encoder.upload[index++] = level_duration_make(true, (uint32_t)4550);
        instance->encoder.upload[index++] =
            level_duration_make(false, (uint32_t)subghz_protocol_somfy_telis_const.te_short);

        for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
            if(bit_read(instance->generic.data, i - 1)) {
                if(instance->encoder.upload[index - 1].level == LEVEL_DURATION_LEVEL_LOW) {
                    instance->encoder.upload[index - 1].duration *= 2;
                    instance->encoder.upload[index++] = level_duration_make(
                        true, (uint32_t)subghz_protocol_somfy_telis_const.te_short);
                } else {
                    instance->encoder.upload[index++] = level_duration_make(
                        false, (uint32_t)subghz_protocol_somfy_telis_const.te_short);
                    instance->encoder.upload[index++] = level_duration_make(
                        true, (uint32_t)subghz_protocol_somfy_telis_const.te_short);
                }

            } else {
                if(instance->encoder.upload[index - 1].level == LEVEL_DURATION_LEVEL_HIGH) {
                    instance->encoder.upload[index - 1].duration *= 2;
                    instance->encoder.upload[index++] = level_duration_make(
                        false, (uint32_t)subghz_protocol_somfy_telis_const.te_short);
                } else {
                    instance->encoder.upload[index++] = level_duration_make(
                        true, (uint32_t)subghz_protocol_somfy_telis_const.te_short);
                    instance->encoder.upload[index++] = level_duration_make(
                        false, (uint32_t)subghz_protocol_somfy_telis_const.te_short);
                }
            }
        }

        if(instance->encoder.upload[index - 1].level == LEVEL_DURATION_LEVEL_LOW) {
            instance->encoder.upload[index - 1].duration += (uint32_t)30415;
        } else {
            instance->encoder.upload[index++] = level_duration_make(false, (uint32_t)30415);
        }
    }

    size_t size_upload = index;

    if(size_upload > instance->encoder.size_upload) {
        FURI_LOG_E(TAG, "Size upload exceeds allocated encoder buffer.");
        return false;
    } else {
        instance->encoder.size_upload = size_upload;
    }
    return true;
}

static uint8_t subghz_protocol_somfy_telis_get_btn_code(void) {
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

        default:
            break;
        }
    }

    return btn;
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

void* subghz_protocol_encoder_somfy_telis_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderSomfyTelis), &subghz_protocol_somfy_telis, 3, 512);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_somfy_telis_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderSomfyTelis* instance = context;
    SubGhzProtocolStatus res = SubGhzProtocolStatusError;
    do {
        /* #16: TxRaw verbatim replay — freeze the rolling counter. */
        somfy_telis_raw_replay = false;
        {
            uint32_t somfy_telis_txraw = 0;
            flipper_format_rewind(flipper_format);
            if(flipper_format_read_uint32(flipper_format, "TxRaw", &somfy_telis_txraw, 1) &&
               somfy_telis_txraw != 0) {
                somfy_telis_raw_replay = true;
            }
            flipper_format_rewind(flipper_format);
        }
        if(SubGhzProtocolStatusOk !=
           subghz_block_generic_deserialize(&instance->generic, flipper_format)) {
            FURI_LOG_E(TAG, "Deserialize error");
            break;
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        subghz_protocol_encoder_somfy_telis_get_upload(instance, instance->generic.btn);

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

        res = SubGhzProtocolStatusOk;
    } while(false);

    return res;
}

static const SubGhzProtocolEncoder enc_somfy_telis_vt = {
    .alloc = subghz_protocol_encoder_somfy_telis_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_somfy_telis_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_somfy_telis = {
    .name = SUBGHZ_PROTOCOL_SOMFY_TELIS_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = NULL,
    .encoder = &enc_somfy_telis_vt,
};

static const SubGhzEncodePluginEntry enc_somfy_telis_entry = {.name = SUBGHZ_PROTOCOL_SOMFY_TELIS_NAME, .encoder = &enc_somfy_telis_vt, .host_slot = &enc_somfy_telis_host_raw};
static const FlipperAppPluginDescriptor enc_somfy_telis_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_somfy_telis_entry};
const FlipperAppPluginDescriptor* enc_somfy_telis_ep(void) { return &enc_somfy_telis_descriptor; }
