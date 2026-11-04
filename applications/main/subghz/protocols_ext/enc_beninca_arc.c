/* enc_beninca_arc.fal : SD encoder for beninca_arc (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_beninca_arc enc_beninca_arc__subghz_protocol_beninca_arc
#define subghz_protocol_encoder_beninca_arc_alloc enc_beninca_arc__subghz_protocol_encoder_beninca_arc_alloc
#define subghz_protocol_encoder_beninca_arc_deserialize enc_beninca_arc__subghz_protocol_encoder_beninca_arc_deserialize
#define subghz_protocol_encoder_common_free enc_beninca_arc__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_beninca_arc__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_beninca_arc__subghz_protocol_encoder_common_yield
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/beninca_arc.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <core/log.h>
#include <stddef.h>
#include <stdint.h>
#include <furi_hal_crypto.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/common.h>

#define TAG "BenincaARC"
#define BENINCA_ARC_KEY_TYPE 9u

struct SubGhzProtocolEncoderBenincaARC {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;

    SubGhzBlockGeneric generic;

    SubGhzKeystore* keystore;
};

static const SubGhzBlockConst subghz_protocol_beninca_arc_const = {
    .te_short = 300,
    .te_long = 600,
    .te_delta = 155,
    .min_count_bit_for_found = 128,
};

/* host bridge: live firmware functions/state this encoder needs (set by the loader) */
extern bool beninca_arc_raw_replay__bridge_ty;
typedef struct {
    void* beninca_arc_raw_replay;
} enc_beninca_arc_host_t;
static const void* enc_beninca_arc_host_raw;
#define ENC_BENINCA_ARC_HOST ((const enc_beninca_arc_host_t*)enc_beninca_arc_host_raw)
#define beninca_arc_raw_replay (*(__typeof__(beninca_arc_raw_replay__bridge_ty)*)(ENC_BENINCA_ARC_HOST->beninca_arc_raw_replay))

static uint8_t subghz_protocol_beninca_arc_get_btn_code(void);
static void get_subghz_protocol_beninca_arc_aes_key(SubGhzKeystore* keystore, uint8_t* aes_key);
static void reverse_bits_in_bytes(uint8_t* data, uint8_t len);
static uint64_t
    subghz_protocol_beninca_arc_decrypt(SubGhzBlockGeneric* generic, SubGhzKeystore* keystore);
static void subghz_protocol_beninca_arc_encrypt(
    SubGhzBlockGeneric* generic,
    SubGhzKeystore* keystore,
    uint64_t middle_bytes);
static void subghz_protocol_beninca_arc_encoder_get_upload(
    SubGhzProtocolEncoderBenincaARC* instance,
    size_t* index);
static void subghz_protocol_beninca_arc_encoder_prepare_packets(
    SubGhzProtocolEncoderBenincaARC* instance);

static uint8_t subghz_protocol_beninca_arc_get_btn_code(void) {
    uint8_t custom_btn_id = subghz_custom_btn_get();
    uint8_t original_btn_code = subghz_custom_btn_get_original();
    uint8_t btn = original_btn_code;

    if((custom_btn_id == SUBGHZ_CUSTOM_BTN_OK) && (original_btn_code != 0)) {

        btn = original_btn_code;
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_UP) {
        switch(original_btn_code) {
        case 0x02:
            btn = 0x04;
            break;
        case 0x04:
            btn = 0x02;
            break;
        case 0x00:
            btn = 0x04;
            break;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_DOWN) {
        switch(original_btn_code) {
        case 0x02:
            btn = 0x00;
            break;
        case 0x04:
            btn = 0x00;
            break;
        case 0x00:
            btn = 0x02;
            break;

        default:
            break;
        }
    }

    return btn;
}

static void get_subghz_protocol_beninca_arc_aes_key(SubGhzKeystore* keystore, uint8_t* aes_key) {
    uint64_t mfkey = 0;
    SUBGHZ_KEYSTORE_FOR_EACH(manufacture_code, keystore) {
            if(manufacture_code->type == BENINCA_ARC_KEY_TYPE) {
                mfkey = manufacture_code->key;
                break;
            }
        }

    uint32_t derived_lo = (uint32_t)(mfkey & 0xFFFFFFFF);
    uint32_t derived_hi = (uint32_t)((mfkey >> 32) & 0xFFFFFFFF);

    uint64_t val64_a = ((uint64_t)derived_hi << 32) | derived_lo;
    for(uint8_t i = 0; i < 8; i++) {
        aes_key[i] = (val64_a >> (56 - i * 8)) & 0xFF;
    }

    uint32_t new_lo = ((derived_hi >> 24) & 0xFF) | ((derived_hi >> 8) & 0xFF00) |
                      ((derived_hi << 8) & 0xFF0000) | ((derived_hi << 24) & 0xFF000000);
    uint32_t new_hi = ((derived_lo >> 24) & 0xFF) | ((derived_lo >> 8) & 0xFF00) |
                      ((derived_lo << 8) & 0xFF0000) | ((derived_lo << 24) & 0xFF000000);

    uint64_t val64_b = ((uint64_t)new_hi << 32) | new_lo;
    for(uint8_t i = 0; i < 8; i++) {
        aes_key[i + 8] = (val64_b >> (56 - i * 8)) & 0xFF;
    }
}

static void reverse_bits_in_bytes(uint8_t* data, uint8_t len) {
    for(uint8_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        uint8_t step1 = ((byte & 0x55) << 1) | ((byte >> 1) & 0x55);
        uint8_t step2 = ((step1 & 0x33) << 2) | ((step1 >> 2) & 0x33);
        data[i] = ((step2 & 0x0F) << 4) | (step2 >> 4);
    }
}

static uint64_t
    subghz_protocol_beninca_arc_decrypt(SubGhzBlockGeneric* generic, SubGhzKeystore* keystore) {

    uint8_t encrypted_data[16];

    for(uint8_t i = 0; i < 8; i++) {
        encrypted_data[i] = (generic->data >> (56 - i * 8)) & 0xFF;
        encrypted_data[i + 8] = (generic->data_2 >> (56 - i * 8)) & 0xFF;
    }

    reverse_bits_in_bytes(encrypted_data, 16);

    uint8_t aes_key[16];
    get_subghz_protocol_beninca_arc_aes_key(keystore, aes_key);

    uint8_t decrypted[16];
    furi_hal_crypto_aes128_ecb_decrypt(aes_key, encrypted_data, decrypted);
    memcpy(encrypted_data, decrypted, 16);

    generic->serial = ((uint32_t)encrypted_data[0] << 24) | ((uint32_t)encrypted_data[1] << 16) |
                      ((uint32_t)encrypted_data[2] << 8) | encrypted_data[3];

    generic->btn = encrypted_data[4];

    uint64_t middle_bytes = 0;
    middle_bytes = ((uint64_t)encrypted_data[5] << 32) | ((uint64_t)encrypted_data[6] << 24) |
                   ((uint64_t)encrypted_data[7] << 16) | ((uint64_t)encrypted_data[8] << 8) |
                   encrypted_data[9];

    generic->cnt = ((uint32_t)encrypted_data[10] << 24) | ((uint32_t)encrypted_data[11] << 16) |
                   ((uint32_t)encrypted_data[12] << 8) | encrypted_data[13];

    generic->seed = ((uint16_t)encrypted_data[14] << 8) | encrypted_data[15];

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(generic->btn);
    }
    subghz_custom_btn_set_max(2);

    return middle_bytes;
}

static void subghz_protocol_beninca_arc_encrypt(
    SubGhzBlockGeneric* generic,
    SubGhzKeystore* keystore,
    uint64_t middle_bytes) {

    uint8_t plaintext[16];

    plaintext[0] = (generic->serial >> 24) & 0xFF;
    plaintext[1] = (generic->serial >> 16) & 0xFF;
    plaintext[2] = (generic->serial >> 8) & 0xFF;
    plaintext[3] = generic->serial & 0xFF;
    plaintext[4] = generic->btn;
    plaintext[5] = (middle_bytes >> 32) & 0xFF;
    plaintext[6] = (middle_bytes >> 24) & 0xFF;
    plaintext[7] = (middle_bytes >> 16) & 0xFF;
    plaintext[8] = (middle_bytes >> 8) & 0xFF;
    plaintext[9] = middle_bytes & 0xFF;
    plaintext[10] = (generic->cnt >> 24) & 0xFF;
    plaintext[11] = (generic->cnt >> 16) & 0xFF;
    plaintext[12] = (generic->cnt >> 8) & 0xFF;
    plaintext[13] = generic->cnt & 0xFF;
    plaintext[14] = (generic->seed >> 8) & 0xFF;
    plaintext[15] = generic->seed & 0xFF;

    uint8_t aes_key[16];
    get_subghz_protocol_beninca_arc_aes_key(keystore, aes_key);

    uint8_t encrypted[16];
    furi_hal_crypto_aes128_ecb_encrypt(aes_key, plaintext, encrypted);
    memcpy(plaintext, encrypted, 16);

    reverse_bits_in_bytes(plaintext, 16);

    for(uint8_t i = 0; i < 8; i++) {
        generic->data = (generic->data << 8) | plaintext[i];
        generic->data_2 = (generic->data_2 << 8) | plaintext[i + 8];
    }
    return;
}

static void subghz_protocol_beninca_arc_encoder_get_upload(
    SubGhzProtocolEncoderBenincaARC* instance,
    size_t* index) {
    furi_assert(instance);
    size_t index_local = *index;

    for(uint8_t i = 64; i > 0; i--) {
        if(bit_read(instance->generic.data, i - 1)) {

            instance->encoder.upload[index_local++] =
                level_duration_make(true, (uint32_t)subghz_protocol_beninca_arc_const.te_short);
            instance->encoder.upload[index_local++] =
                level_duration_make(false, (uint32_t)subghz_protocol_beninca_arc_const.te_long);
        } else {

            instance->encoder.upload[index_local++] =
                level_duration_make(true, (uint32_t)subghz_protocol_beninca_arc_const.te_long);
            instance->encoder.upload[index_local++] =
                level_duration_make(false, (uint32_t)subghz_protocol_beninca_arc_const.te_short);
        }
    }

    for(uint8_t i = 64; i > 0; i--) {
        if(bit_read(instance->generic.data_2, i - 1)) {

            instance->encoder.upload[index_local++] =
                level_duration_make(true, (uint32_t)subghz_protocol_beninca_arc_const.te_short);
            instance->encoder.upload[index_local++] =
                level_duration_make(false, (uint32_t)subghz_protocol_beninca_arc_const.te_long);
        } else {

            instance->encoder.upload[index_local++] =
                level_duration_make(true, (uint32_t)subghz_protocol_beninca_arc_const.te_long);
            instance->encoder.upload[index_local++] =
                level_duration_make(false, (uint32_t)subghz_protocol_beninca_arc_const.te_short);
        }
    }

    instance->encoder.upload[index_local++] =
        level_duration_make(true, (uint32_t)subghz_protocol_beninca_arc_const.te_short);

    instance->encoder.upload[index_local++] =
        level_duration_make(false, (uint32_t)subghz_protocol_beninca_arc_const.te_long * 15);

    *index = index_local;
}

static void subghz_protocol_beninca_arc_encoder_prepare_packets(
    SubGhzProtocolEncoderBenincaARC* instance) {
    furi_assert(instance);

    if(beninca_arc_raw_replay) {
        /* #16: freeze counter on TxRaw verbatim replay */
    } else if(furi_hal_subghz_get_rolling_counter_mult() != -0x7FFFFFFF) {

        if(!subghz_block_generic_global_counter_override_get(&instance->generic.cnt)) {

            if((instance->generic.cnt + furi_hal_subghz_get_rolling_counter_mult()) > 0xFFFFFFFF) {
                instance->generic.cnt = 0;
            } else {
                instance->generic.cnt += furi_hal_subghz_get_rolling_counter_mult();
            }
        }
    } else {

        instance->generic.cnt += 1;
    }

    size_t index = 0;

    instance->generic.btn = subghz_protocol_beninca_arc_get_btn_code();

    if(subghz_block_generic_global_button_override_get(&instance->generic.btn))
        FURI_LOG_D(TAG, "Button sucessfully changed to 0x%X", instance->generic.btn);

    for(uint8_t i = 0; i < 3; i++) {
        subghz_protocol_beninca_arc_encrypt(
            &instance->generic, instance->keystore, (uint64_t)((i + 1) * 2));
        subghz_protocol_beninca_arc_encoder_get_upload(instance, &index);
    }

    instance->encoder.size_upload = index;
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

void* subghz_protocol_encoder_beninca_arc_alloc(SubGhzEnvironment* environment) {
    SubGhzProtocolEncoderBenincaARC* instance = malloc(sizeof(SubGhzProtocolEncoderBenincaARC));

    instance->base.protocol = &subghz_protocol_beninca_arc;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->keystore = subghz_environment_get_keystore(environment);

    instance->encoder.repeat = 1;
    instance->encoder.size_upload = 800;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;

    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_beninca_arc_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderBenincaARC* instance = context;
    SubGhzProtocolStatus res = SubGhzProtocolStatusError;
    do {
        /* #16: TxRaw verbatim replay — freeze the rolling counter. */
        beninca_arc_raw_replay = false;
        {
            uint32_t beninca_arc_txraw = 0;
            flipper_format_rewind(flipper_format);
            if(flipper_format_read_uint32(flipper_format, "TxRaw", &beninca_arc_txraw, 1) &&
               beninca_arc_txraw != 0) {
                beninca_arc_raw_replay = true;
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

        subghz_protocol_beninca_arc_decrypt(&instance->generic, instance->keystore);

        subghz_protocol_beninca_arc_encoder_prepare_packets(instance);

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

static const SubGhzProtocolEncoder enc_beninca_arc_vt = {
    .alloc = subghz_protocol_encoder_beninca_arc_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_beninca_arc_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_beninca_arc = {
    .name = SUBGHZ_PROTOCOL_BENINCA_ARC_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = NULL,
    .encoder = &enc_beninca_arc_vt,
};

static const SubGhzEncodePluginEntry enc_beninca_arc_entry = {.name = SUBGHZ_PROTOCOL_BENINCA_ARC_NAME, .encoder = &enc_beninca_arc_vt, .host_slot = &enc_beninca_arc_host_raw};
static const FlipperAppPluginDescriptor enc_beninca_arc_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_beninca_arc_entry};
const FlipperAppPluginDescriptor* enc_beninca_arc_ep(void) { return &enc_beninca_arc_descriptor; }
