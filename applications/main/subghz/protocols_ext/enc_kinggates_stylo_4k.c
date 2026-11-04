/* enc_kinggates_stylo_4k.fal : SD encoder for kinggates_stylo_4k (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_free enc_kinggates_stylo_4k__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_kinggates_stylo_4k__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_kinggates_stylo_4k__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_kinggates_stylo_4k_alloc enc_kinggates_stylo_4k__subghz_protocol_encoder_kinggates_stylo_4k_alloc
#define subghz_protocol_encoder_kinggates_stylo_4k_deserialize enc_kinggates_stylo_4k__subghz_protocol_encoder_kinggates_stylo_4k_deserialize
#define subghz_protocol_kinggates_stylo_4k enc_kinggates_stylo_4k__subghz_protocol_kinggates_stylo_4k
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/kinggates_stylo_4k.h>
#include <lib/subghz/protocols/keeloq_common.h>
#include <lib/subghz/subghz_keystore.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocoKingGatesStylo4k"

struct SubGhzProtocolEncoderKingGates_stylo_4k {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    SubGhzKeystore* keystore;
};

static const SubGhzBlockConst subghz_protocol_kinggates_stylo_4k_const = {
    .te_short = 400,
    .te_long = 1100,
    .te_delta = 140,
    .min_count_bit_for_found = 89,
};

/* host bridge: live firmware functions/state this encoder needs (set by the loader) */
extern bool kinggates_stylo_4k_raw_replay__bridge_ty;
typedef struct {
    void* kinggates_stylo_4k_raw_replay;
    __typeof__(&subghz_protocol_keeloq_common_decrypt) subghz_protocol_keeloq_common_decrypt;
    __typeof__(&subghz_protocol_keeloq_common_encrypt) subghz_protocol_keeloq_common_encrypt;
} enc_kinggates_stylo_4k_host_t;
static const void* enc_kinggates_stylo_4k_host_raw;
#define ENC_KINGGATES_STYLO_4K_HOST ((const enc_kinggates_stylo_4k_host_t*)enc_kinggates_stylo_4k_host_raw)
#define kinggates_stylo_4k_raw_replay (*(__typeof__(kinggates_stylo_4k_raw_replay__bridge_ty)*)(ENC_KINGGATES_STYLO_4K_HOST->kinggates_stylo_4k_raw_replay))
#define subghz_protocol_keeloq_common_decrypt (ENC_KINGGATES_STYLO_4K_HOST->subghz_protocol_keeloq_common_decrypt)
#define subghz_protocol_keeloq_common_encrypt (ENC_KINGGATES_STYLO_4K_HOST->subghz_protocol_keeloq_common_encrypt)

static bool subghz_protocol_kinggates_stylo_4k_gen_data(
    SubGhzProtocolEncoderKingGates_stylo_4k* instance,
    uint8_t btn);
static bool subghz_protocol_encoder_kinggates_stylo_4k_get_upload(
    SubGhzProtocolEncoderKingGates_stylo_4k* instance,
    uint8_t btn);
static void subghz_protocol_kinggates_stylo_4k_remote_controller(
    SubGhzBlockGeneric* instance,
    SubGhzKeystore* keystore);
static uint8_t subghz_protocol_kinggates_stylo_4k_get_btn_code(void);

static bool subghz_protocol_kinggates_stylo_4k_gen_data(
    SubGhzProtocolEncoderKingGates_stylo_4k* instance,
    uint8_t btn) {

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(btn);
    }

    btn = subghz_protocol_kinggates_stylo_4k_get_btn_code();

    if(subghz_block_generic_global_button_override_get(&btn))
        FURI_LOG_D(TAG, "Button sucessfully changed to 0x%X", btn);

    if(kinggates_stylo_4k_raw_replay) {
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

    uint32_t hop = (uint64_t)btn << 28 | (((uint32_t)0x0C) << 24) |
                   ((instance->generic.serial & 0xFF) << 16) | (instance->generic.cnt & 0xFFFF);

    uint64_t fix = ((uint64_t)((instance->generic.serial >> 16) & 0xFFFF) << 21) |
                   (uint64_t)btn << 17 | 0b1 << 16 | (instance->generic.serial & 0xFFFF);

    instance->generic.data = subghz_protocol_blocks_reverse_key(fix, 53);

    uint64_t encrypt = 0;
    SUBGHZ_KEYSTORE_FOR_EACH(manufacture_code, instance->keystore) {
            if(manufacture_code->type == KEELOQ_LEARNING_SIMPLE_KINGGATES) {

                encrypt = subghz_protocol_keeloq_common_encrypt(hop, manufacture_code->key);
                encrypt = subghz_protocol_blocks_reverse_key(encrypt, 32);
                instance->generic.data_2 = encrypt << 4;
                return true;
            }
        }

    return false;
}

static bool subghz_protocol_encoder_kinggates_stylo_4k_get_upload(
    SubGhzProtocolEncoderKingGates_stylo_4k* instance,
    uint8_t btn) {
    furi_assert(instance);

    if(!subghz_protocol_kinggates_stylo_4k_gen_data(instance, btn)) {
        return false;
    }

    size_t index = 0;

    instance->encoder.upload[index++] = level_duration_make(false, (uint32_t)9500);

    for(uint8_t i = 12; i > 0; i--) {
        instance->encoder.upload[index++] =
            level_duration_make(true, (uint32_t)subghz_protocol_kinggates_stylo_4k_const.te_short);
        instance->encoder.upload[index++] = level_duration_make(
            false, (uint32_t)subghz_protocol_kinggates_stylo_4k_const.te_short);
    }

    instance->encoder.upload[index - 1].duration =
        (uint32_t)subghz_protocol_kinggates_stylo_4k_const.te_long * 2;
    instance->encoder.upload[index++] =
        level_duration_make(true, (uint32_t)subghz_protocol_kinggates_stylo_4k_const.te_short * 2);

    for(uint8_t i = 53; i > 0; i--) {
        if(bit_read(instance->generic.data, i - 1)) {

            instance->encoder.upload[index++] = level_duration_make(
                false, (uint32_t)subghz_protocol_kinggates_stylo_4k_const.te_short);
            instance->encoder.upload[index++] = level_duration_make(
                true, (uint32_t)subghz_protocol_kinggates_stylo_4k_const.te_long);
        } else {

            instance->encoder.upload[index++] = level_duration_make(
                false, (uint32_t)subghz_protocol_kinggates_stylo_4k_const.te_long);
            instance->encoder.upload[index++] = level_duration_make(
                true, (uint32_t)subghz_protocol_kinggates_stylo_4k_const.te_short);
        }
    }

    for(uint8_t i = 36; i > 0; i--) {
        if(bit_read(instance->generic.data_2, i - 1)) {

            instance->encoder.upload[index++] = level_duration_make(
                false, (uint32_t)subghz_protocol_kinggates_stylo_4k_const.te_short);
            instance->encoder.upload[index++] = level_duration_make(
                true, (uint32_t)subghz_protocol_kinggates_stylo_4k_const.te_long);
        } else {

            instance->encoder.upload[index++] = level_duration_make(
                false, (uint32_t)subghz_protocol_kinggates_stylo_4k_const.te_long);
            instance->encoder.upload[index++] = level_duration_make(
                true, (uint32_t)subghz_protocol_kinggates_stylo_4k_const.te_short);
        }
    }

    instance->encoder.size_upload = index;

    return true;
}

static void subghz_protocol_kinggates_stylo_4k_remote_controller(
    SubGhzBlockGeneric* instance,
    SubGhzKeystore* keystore) {

    uint32_t hop = subghz_protocol_blocks_reverse_key(instance->data_2 >> 4, 32);
    uint64_t fix = subghz_protocol_blocks_reverse_key(instance->data, 53);
    bool ret = false;
    uint32_t decrypt = 0;
    instance->btn = (fix >> 17) & 0x0F;
    instance->serial = ((fix >> 5) & 0xFFFF0000) | (fix & 0xFFFF);

    SUBGHZ_KEYSTORE_FOR_EACH(manufacture_code, keystore) {
            if(manufacture_code->type == KEELOQ_LEARNING_SIMPLE_KINGGATES) {
                decrypt = subghz_protocol_keeloq_common_decrypt(hop, manufacture_code->key);
                if(((decrypt >> 28) == instance->btn) && (((decrypt >> 24) & 0x0F) == 0x0C) &&
                   (((decrypt >> 16) & 0xFF) == (instance->serial & 0xFF))) {
                    ret = true;
                    break;
                }
            }
        }
    if(ret) {
        instance->cnt = decrypt & 0xFFFF;

        if(subghz_custom_btn_get_original() == 0) {
            subghz_custom_btn_set_original(instance->btn);
        }
        subghz_custom_btn_set_max(3);
    } else {
        instance->btn = 0;
        instance->serial = 0;
        instance->cnt = 0;
    }
}

static uint8_t subghz_protocol_kinggates_stylo_4k_get_btn_code(void) {
    uint8_t custom_btn_id = subghz_custom_btn_get();
    uint8_t original_btn_code = subghz_custom_btn_get_original();
    uint8_t btn = original_btn_code;

    if((custom_btn_id == SUBGHZ_CUSTOM_BTN_OK) && (original_btn_code != 0)) {

        btn = original_btn_code;
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_UP) {
        switch(original_btn_code) {
        case 0xE:
            btn = 0xD;
            break;
        case 0xD:
            btn = 0xE;
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
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_DOWN) {
        switch(original_btn_code) {
        case 0xE:
            btn = 0xB;
            break;
        case 0xD:
            btn = 0xB;
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
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_LEFT) {
        switch(original_btn_code) {
        case 0xE:
            btn = 0x7;
            break;
        case 0xD:
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

void* subghz_protocol_encoder_kinggates_stylo_4k_alloc(SubGhzEnvironment* environment) {
    SubGhzProtocolEncoderKingGates_stylo_4k* instance =
        malloc(sizeof(SubGhzProtocolEncoderKingGates_stylo_4k));

    instance->base.protocol = &subghz_protocol_kinggates_stylo_4k;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->keystore = subghz_environment_get_keystore(environment);

    instance->encoder.repeat = 3;
    instance->encoder.size_upload = 512;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;

    return instance;
}

SubGhzProtocolStatus subghz_protocol_encoder_kinggates_stylo_4k_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderKingGates_stylo_4k* instance = context;
    SubGhzProtocolStatus res = SubGhzProtocolStatusError;
    do {
        /* #16: TxRaw verbatim replay — freeze the rolling counter. */
        kinggates_stylo_4k_raw_replay = false;
        {
            uint32_t kinggates_stylo_4k_txraw = 0;
            flipper_format_rewind(flipper_format);
            if(flipper_format_read_uint32(flipper_format, "TxRaw", &kinggates_stylo_4k_txraw, 1) &&
               kinggates_stylo_4k_txraw != 0) {
                kinggates_stylo_4k_raw_replay = true;
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

        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }

        uint8_t key_data[sizeof(uint64_t)] = {0};
        if(!flipper_format_read_hex(flipper_format, "Data", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Missing Data");
            break;
        }

        for(uint8_t i = 0; i < sizeof(uint64_t); i++) {
            instance->generic.data_2 = instance->generic.data_2 << 8 | key_data[i];
        }

        subghz_protocol_kinggates_stylo_4k_remote_controller(
            &instance->generic, instance->keystore);

        subghz_protocol_encoder_kinggates_stylo_4k_get_upload(instance, instance->generic.btn);

        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }

        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            key_data[sizeof(uint64_t) - i - 1] = (instance->generic.data >> i * 8) & 0xFF;
        }
        if(!flipper_format_update_hex(flipper_format, "Key", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Unable to update Key");
            break;
        }

        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            key_data[sizeof(uint64_t) - i - 1] = (instance->generic.data_2 >> i * 8) & 0xFF;
        }
        if(!flipper_format_update_hex(flipper_format, "Data", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Unable to update Data");
            break;
        }

        instance->encoder.is_running = true;

        res = SubGhzProtocolStatusOk;
    } while(false);

    return res;
}

static const SubGhzProtocolEncoder enc_kinggates_stylo_4k_vt = {
    .alloc = subghz_protocol_encoder_kinggates_stylo_4k_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_kinggates_stylo_4k_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_kinggates_stylo_4k = {
    .name = SUBGHZ_PROTOCOL_KINGGATES_STYLO_4K_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = NULL,
    .encoder = &enc_kinggates_stylo_4k_vt,
};

static const SubGhzEncodePluginEntry enc_kinggates_stylo_4k_entry = {.name = SUBGHZ_PROTOCOL_KINGGATES_STYLO_4K_NAME, .encoder = &enc_kinggates_stylo_4k_vt, .host_slot = &enc_kinggates_stylo_4k_host_raw};
static const FlipperAppPluginDescriptor enc_kinggates_stylo_4k_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_kinggates_stylo_4k_entry};
const FlipperAppPluginDescriptor* enc_kinggates_stylo_4k_ep(void) { return &enc_kinggates_stylo_4k_descriptor; }
