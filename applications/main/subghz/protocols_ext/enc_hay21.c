/* enc_hay21.fal : SD encoder for hay21 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_alloc enc_hay21__subghz_protocol_encoder_common_alloc
#define subghz_protocol_encoder_common_free enc_hay21__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_hay21__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_hay21__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_hay21_alloc enc_hay21__subghz_protocol_encoder_hay21_alloc
#define subghz_protocol_encoder_hay21_deserialize enc_hay21__subghz_protocol_encoder_hay21_deserialize
#define subghz_protocol_hay21 enc_hay21__subghz_protocol_hay21
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/hay21.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolHay21"

struct SubGhzProtocolEncoderHay21 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_hay21_const = {
    .te_short = 300,
    .te_long = 700,
    .te_delta = 150,
    .min_count_bit_for_found = 21,
};

/* host bridge: live firmware functions/state this encoder needs (set by the loader) */
extern bool hay21_raw_replay__bridge_ty;
typedef struct {
    void* hay21_raw_replay;
} enc_hay21_host_t;
static const void* enc_hay21_host_raw;
#define ENC_HAY21_HOST ((const enc_hay21_host_t*)enc_hay21_host_raw)
#define hay21_raw_replay (*(__typeof__(hay21_raw_replay__bridge_ty)*)(ENC_HAY21_HOST->hay21_raw_replay))

static uint8_t subghz_protocol_hay21_get_btn_code(void);
static void subghz_protocol_encoder_hay21_get_upload(SubGhzProtocolEncoderHay21* instance);
static void subghz_protocol_hay21_remote_controller(SubGhzBlockGeneric* instance);

static uint8_t subghz_protocol_hay21_get_btn_code(void) {
    uint8_t custom_btn_id = subghz_custom_btn_get();
    uint8_t original_btn_code = subghz_custom_btn_get_original();
    uint8_t btn = original_btn_code;

    if((custom_btn_id == SUBGHZ_CUSTOM_BTN_OK) && (original_btn_code != 0)) {

        btn = original_btn_code;
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_UP) {
        switch(original_btn_code) {
        case 0x5A:
            btn = 0xC3;
            break;
        case 0xC3:
            btn = 0x5A;
            break;
        case 0x88:
            btn = 0x5A;
            break;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_DOWN) {
        switch(original_btn_code) {
        case 0x5A:
            btn = 0x88;
            break;
        case 0xC3:
            btn = 0x88;
            break;
        case 0x88:
            btn = 0xC3;
            break;

        default:
            break;
        }
    }

    return btn;
}

static void subghz_protocol_encoder_hay21_get_upload(SubGhzProtocolEncoderHay21* instance) {
    furi_assert(instance);

    instance->generic.btn = subghz_protocol_hay21_get_btn_code();

    if(subghz_block_generic_global_button_override_get(&instance->generic.btn))
        FURI_LOG_D(TAG, "Button sucessfully changed to 0x%X", instance->generic.btn);

    if(hay21_raw_replay) {
        /* #16: freeze counter on TxRaw verbatim replay */
    } else if(furi_hal_subghz_get_rolling_counter_mult() != -0x7FFFFFFF) {

        if(!subghz_block_generic_global_counter_override_get(&instance->generic.cnt)) {

            if((instance->generic.cnt + furi_hal_subghz_get_rolling_counter_mult()) > 0xF) {
                instance->generic.cnt = 0;
            } else {
                instance->generic.cnt += furi_hal_subghz_get_rolling_counter_mult();
            }
        }
    } else {

        if((instance->generic.cnt + 0x1) > 0xF) {
            instance->generic.cnt = 0;
        } else if(instance->generic.cnt >= 0x1 && instance->generic.cnt != 0xE) {
            instance->generic.cnt = 0xE;
        } else {
            instance->generic.cnt++;
        }
    }

    instance->generic.data =
        ((uint64_t)instance->generic.btn << 13 | (uint64_t)instance->generic.serial << 5 |
         instance->generic.cnt << 1) |
        0b1;

    size_t index = 0;

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if(bit_read(instance->generic.data, i - 1)) {

            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_hay21_const.te_long);
            if(i == 1) {

                instance->encoder.upload[index++] =
                    level_duration_make(false, (uint32_t)subghz_protocol_hay21_const.te_long * 6);
            } else {
                instance->encoder.upload[index++] =
                    level_duration_make(false, (uint32_t)subghz_protocol_hay21_const.te_short);
            }
        } else {

            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_hay21_const.te_short);
            if(i == 1) {

                instance->encoder.upload[index++] =
                    level_duration_make(false, (uint32_t)subghz_protocol_hay21_const.te_long * 6);
            } else {
                instance->encoder.upload[index++] =
                    level_duration_make(false, (uint32_t)subghz_protocol_hay21_const.te_long);
            }
        }
    }

    instance->encoder.size_upload = index;
    return;
}

static void subghz_protocol_hay21_remote_controller(SubGhzBlockGeneric* instance) {
    instance->btn = (instance->data >> 13) & 0xFF;
    instance->serial = (instance->data >> 5) & 0xFF;
    instance->cnt = (instance->data >> 1) & 0xF;

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->btn);
    }
    subghz_custom_btn_set_max(2);

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

void* subghz_protocol_encoder_hay21_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderHay21), &subghz_protocol_hay21, 3, 64);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_hay21_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderHay21* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        /* #16: TxRaw verbatim replay — freeze the rolling counter. */
        hay21_raw_replay = false;
        {
            uint32_t hay21_txraw = 0;
            flipper_format_rewind(flipper_format);
            if(flipper_format_read_uint32(flipper_format, "TxRaw", &hay21_txraw, 1) &&
               hay21_txraw != 0) {
                hay21_raw_replay = true;
            }
            flipper_format_rewind(flipper_format);
        }
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_hay21_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        subghz_protocol_hay21_remote_controller(&instance->generic);
        subghz_protocol_encoder_hay21_get_upload(instance);

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

static const SubGhzProtocolEncoder enc_hay21_vt = {
    .alloc = subghz_protocol_encoder_hay21_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_hay21_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_hay21 = {
    .name = SUBGHZ_PROTOCOL_HAY21_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = NULL,
    .encoder = &enc_hay21_vt,
};

static const SubGhzEncodePluginEntry enc_hay21_entry = {.name = SUBGHZ_PROTOCOL_HAY21_NAME, .encoder = &enc_hay21_vt, .host_slot = &enc_hay21_host_raw};
static const FlipperAppPluginDescriptor enc_hay21_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_hay21_entry};
const FlipperAppPluginDescriptor* enc_hay21_ep(void) { return &enc_hay21_descriptor; }
