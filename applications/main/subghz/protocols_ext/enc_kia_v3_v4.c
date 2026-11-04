/* enc_kia_v3_v4.fal : SD encoder for kia_v3_v4 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_kia_v3_v4_alloc enc_kia_v3_v4__subghz_protocol_encoder_kia_v3_v4_alloc
#define subghz_protocol_encoder_kia_v3_v4_deserialize enc_kia_v3_v4__subghz_protocol_encoder_kia_v3_v4_deserialize
#define subghz_protocol_encoder_kia_v3_v4_free enc_kia_v3_v4__subghz_protocol_encoder_kia_v3_v4_free
#define subghz_protocol_encoder_kia_v3_v4_stop enc_kia_v3_v4__subghz_protocol_encoder_kia_v3_v4_stop
#define subghz_protocol_encoder_kia_v3_v4_yield enc_kia_v3_v4__subghz_protocol_encoder_kia_v3_v4_yield
#define subghz_protocol_kia_v3_v4 enc_kia_v3_v4__subghz_protocol_kia_v3_v4
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/kia_v3_v4.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/keeloq_common.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolKiaV3V4"
#define KIA_MF_KEY 0xA8F5DFFC8DAA5CDBULL
#define KIA_MF_KEY_2 0xCB72B95709057920ULL
#define KIA_V3_V4_PREAMBLE_PAIRS  12U
#define KIA_V3_V4_BIT_COUNT       64U
#define KIA_V3_V4_CRC_BIT_COUNT   4U
#define KIA_V3_V4_CRC_SWEEP_COUNT 16U
#define KIA_V3_V4_SYNC_DURATION   1200U
#define KIA_V3_V4_END_MARKER_US   800U
#define KIA_V3_V4_DEFAULT_REPEAT  KIA_V3_V4_CRC_SWEEP_COUNT
#define KIA_V3_V4_DATA_OFFSET   ((KIA_V3_V4_PREAMBLE_PAIRS * 2U) + 2U)
#define KIA_V3_V4_CRC_OFFSET    (KIA_V3_V4_DATA_OFFSET + (KIA_V3_V4_BIT_COUNT * 2U))
#define KIA_V3_V4_END_OFFSET    (KIA_V3_V4_CRC_OFFSET + (KIA_V3_V4_CRC_BIT_COUNT * 2U))
#define KIA_V3_V4_BURST_ENTRIES (KIA_V3_V4_END_OFFSET + 2U)
#define KIA_V3_V4_UPLOAD_CAPACITY KIA_V3_V4_BURST_ENTRIES

struct SubGhzProtocolEncoderKiaV3V4 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint32_t serial;
    uint8_t btn;
    uint16_t cnt;
    uint8_t version;

    uint32_t encrypted;
    uint32_t decrypted;
    uint64_t mf_key;

    uint8_t crc_iter;
    uint8_t bursts_sent;
};

static const SubGhzBlockConst subghz_protocol_kia_v3_v4_const = {
    .te_short = 400,
    .te_long = 800,
    .te_delta = 150,
    .min_count_bit_for_found = 68,
};

/* host bridge: live firmware functions/state this encoder needs (set by the loader) */
typedef struct {
    __typeof__(&subghz_protocol_keeloq_common_decrypt) subghz_protocol_keeloq_common_decrypt;
    __typeof__(&subghz_protocol_keeloq_common_encrypt) subghz_protocol_keeloq_common_encrypt;
} enc_kia_v3_v4_host_t;
static const void* enc_kia_v3_v4_host_raw;
#define ENC_KIA_V3_V4_HOST ((const enc_kia_v3_v4_host_t*)enc_kia_v3_v4_host_raw)
#define subghz_protocol_keeloq_common_decrypt (ENC_KIA_V3_V4_HOST->subghz_protocol_keeloq_common_decrypt)
#define subghz_protocol_keeloq_common_encrypt (ENC_KIA_V3_V4_HOST->subghz_protocol_keeloq_common_encrypt)

static uint8_t kia_v3_v4_reverse8(uint8_t byte);
// Kia/Hyundai V3/V4 manufacturer keys. Both are genuine (KIA_MF_KEY_2 is the
// second key some models use, added 2026-10-03). Decode tries each with a
// serial/button self-check; the matching key is reused for re-encrypt so a
// re-transmitted frame stays valid for that car.
static uint64_t
    kia_v3_v4_select_mf_key(uint32_t encrypted, uint8_t btn, uint8_t serial_lsb);
// Recover the captured 32-bit encrypted word from the stored 68-bit Key
// (generic.data), using the same byte layout the decoder built it from.
static uint32_t kia_v3_v4_encrypted_from_keydata(uint64_t data);
static inline void kia_v3_v4_emit_bit_pwm(LevelDuration* upload, size_t* idx, bool bit, bool v4);
static uint64_t kia_v3_v4_build_tx_bitstream(SubGhzProtocolEncoderKiaV3V4* instance);
static void subghz_protocol_encoder_kia_v3_v4_build_packet(
    SubGhzProtocolEncoderKiaV3V4* instance,
    uint8_t* raw_bytes);
static void subghz_protocol_encoder_kia_v3_v4_patch_crc(SubGhzProtocolEncoderKiaV3V4* instance);
static void subghz_protocol_encoder_kia_v3_v4_get_upload(SubGhzProtocolEncoderKiaV3V4* instance);

static uint8_t kia_v3_v4_reverse8(uint8_t byte) {
    byte = (byte & 0xF0) >> 4 | (byte & 0x0F) << 4;
    byte = (byte & 0xCC) >> 2 | (byte & 0x33) << 2;
    byte = (byte & 0xAA) >> 1 | (byte & 0x55) << 1;
    return byte;
}

// Kia/Hyundai V3/V4 manufacturer keys. Both are genuine (KIA_MF_KEY_2 is the
// second key some models use, added 2026-10-03). Decode tries each with a
// serial/button self-check; the matching key is reused for re-encrypt so a
// re-transmitted frame stays valid for that car.
static uint64_t
    kia_v3_v4_select_mf_key(uint32_t encrypted, uint8_t btn, uint8_t serial_lsb) {
    static const uint64_t keys[2] = {KIA_MF_KEY, KIA_MF_KEY_2};
    for(size_t i = 0; i < 2; i++) {
        uint32_t dec = subghz_protocol_keeloq_common_decrypt(encrypted, keys[i]);
        if(((dec >> 28) & 0x0F) == btn && ((dec >> 16) & 0xFF) == serial_lsb) {
            return keys[i];
        }
    }
    return 0;
}

// Recover the captured 32-bit encrypted word from the stored 68-bit Key
// (generic.data), using the same byte layout the decoder built it from.
static uint32_t kia_v3_v4_encrypted_from_keydata(uint64_t data) {
    uint8_t b0 = (uint8_t)((data >> 56) & 0xFF);
    uint8_t b1 = (uint8_t)((data >> 48) & 0xFF);
    uint8_t b2 = (uint8_t)((data >> 40) & 0xFF);
    uint8_t b3 = (uint8_t)((data >> 32) & 0xFF);
    return ((uint32_t)kia_v3_v4_reverse8(b3) << 24) | ((uint32_t)kia_v3_v4_reverse8(b2) << 16) |
           ((uint32_t)kia_v3_v4_reverse8(b1) << 8) | (uint32_t)kia_v3_v4_reverse8(b0);
}

static inline void kia_v3_v4_emit_bit_pwm(LevelDuration* upload, size_t* idx, bool bit, bool v4) {
    const uint32_t te_short = subghz_protocol_kia_v3_v4_const.te_short;
    const uint32_t te_long = subghz_protocol_kia_v3_v4_const.te_long;
    const uint32_t first_us = bit ? te_short : te_long;
    const uint32_t second_us = bit ? te_long : te_short;

    if(v4) {
        upload[(*idx)++] = level_duration_make(false, (int32_t)first_us);
        upload[(*idx)++] = level_duration_make(true, (int32_t)second_us);
    } else {
        upload[(*idx)++] = level_duration_make(true, (int32_t)first_us);
        upload[(*idx)++] = level_duration_make(false, (int32_t)second_us);
    }
}

static uint64_t kia_v3_v4_build_tx_bitstream(SubGhzProtocolEncoderKiaV3V4* instance) {
    const uint32_t serial_btn = (instance->serial & 0x0FFFFFFFU) |
                                ((uint32_t)(instance->btn & 0x0FU) << 28);
    const uint64_t key = ((uint64_t)serial_btn << 32) | (uint64_t)instance->encrypted;
    return subghz_protocol_blocks_reverse_key(key, 64);
}

static void subghz_protocol_encoder_kia_v3_v4_build_packet(
    SubGhzProtocolEncoderKiaV3V4* instance,
    uint8_t* raw_bytes) {
    uint32_t plaintext = (uint32_t)(instance->cnt & 0xFFFFU) |
                         ((uint32_t)(instance->serial & 0x3FFU) << 16) |
                         ((uint32_t)(instance->btn & 0x0FU) << 28);

    instance->decrypted = plaintext;

    uint64_t mf_key = instance->mf_key ? instance->mf_key : KIA_MF_KEY;
    uint32_t encrypted = subghz_protocol_keeloq_common_encrypt(plaintext, mf_key);
    instance->encrypted = encrypted;

    raw_bytes[0] = kia_v3_v4_reverse8((encrypted >> 0) & 0xFF);
    raw_bytes[1] = kia_v3_v4_reverse8((encrypted >> 8) & 0xFF);
    raw_bytes[2] = kia_v3_v4_reverse8((encrypted >> 16) & 0xFF);
    raw_bytes[3] = kia_v3_v4_reverse8((encrypted >> 24) & 0xFF);

    uint32_t serial_btn = (instance->serial & 0x0FFFFFFFU) |
                          ((uint32_t)(instance->btn & 0x0F) << 28);
    raw_bytes[4] = kia_v3_v4_reverse8((serial_btn >> 0) & 0xFF);
    raw_bytes[5] = kia_v3_v4_reverse8((serial_btn >> 8) & 0xFF);
    raw_bytes[6] = kia_v3_v4_reverse8((serial_btn >> 16) & 0xFF);
    raw_bytes[7] = kia_v3_v4_reverse8((serial_btn >> 24) & 0xFF);

    instance->generic.data = ((uint64_t)raw_bytes[0] << 56) | ((uint64_t)raw_bytes[1] << 48) |
                             ((uint64_t)raw_bytes[2] << 40) | ((uint64_t)raw_bytes[3] << 32) |
                             ((uint64_t)raw_bytes[4] << 24) | ((uint64_t)raw_bytes[5] << 16) |
                             ((uint64_t)raw_bytes[6] << 8) | (uint64_t)raw_bytes[7];
    instance->generic.data_count_bit = 68;
}

static void subghz_protocol_encoder_kia_v3_v4_patch_crc(SubGhzProtocolEncoderKiaV3V4* instance) {
    if(!instance || !instance->encoder.upload) return;
    const bool v4 = (instance->version == 0);
    const uint8_t crc = instance->crc_iter & 0x0FU;
    size_t idx = KIA_V3_V4_CRC_OFFSET;
    for(int b = 3; b >= 0; b--) {
        const bool bit = (crc >> b) & 1U;
        kia_v3_v4_emit_bit_pwm(instance->encoder.upload, &idx, bit, v4);
    }
}

static void subghz_protocol_encoder_kia_v3_v4_get_upload(SubGhzProtocolEncoderKiaV3V4* instance) {
    furi_assert(instance);

    uint8_t raw_bytes[8];
    subghz_protocol_encoder_kia_v3_v4_build_packet(instance, raw_bytes);

    const bool v4 = (instance->version == 0);
    const uint64_t tx_key = kia_v3_v4_build_tx_bitstream(instance);

    size_t idx = 0;
    LevelDuration* upload = instance->encoder.upload;
    const uint32_t te_short = subghz_protocol_kia_v3_v4_const.te_short;

    for(uint32_t i = 0; i < KIA_V3_V4_PREAMBLE_PAIRS; i++) {
        if(v4) {
            upload[idx++] = level_duration_make(false, (int32_t)te_short);
            upload[idx++] = level_duration_make(true, (int32_t)te_short);
        } else {
            upload[idx++] = level_duration_make(true, (int32_t)te_short);
            upload[idx++] = level_duration_make(false, (int32_t)te_short);
        }
    }

    if(v4) {
        upload[idx++] = level_duration_make(false, (int32_t)te_short);
        upload[idx++] = level_duration_make(true, (int32_t)KIA_V3_V4_SYNC_DURATION);
    } else {
        upload[idx++] = level_duration_make(true, (int32_t)te_short);
        upload[idx++] = level_duration_make(false, (int32_t)KIA_V3_V4_SYNC_DURATION);
    }

    for(int i = 63; i >= 0; i--) {
        const bool bit = (tx_key >> i) & 1ULL;
        kia_v3_v4_emit_bit_pwm(upload, &idx, bit, v4);
    }

    const uint8_t crc = instance->crc_iter & 0x0FU;
    for(int b = 3; b >= 0; b--) {
        const bool bit = (crc >> b) & 1U;
        kia_v3_v4_emit_bit_pwm(upload, &idx, bit, v4);
    }

    if(v4) {
        upload[idx++] = level_duration_make(false, (int32_t)KIA_V3_V4_END_MARKER_US);
        upload[idx++] = level_duration_make(true, (int32_t)KIA_V3_V4_END_MARKER_US);
    } else {
        upload[idx++] = level_duration_make(true, (int32_t)KIA_V3_V4_END_MARKER_US);
        upload[idx++] = level_duration_make(false, (int32_t)KIA_V3_V4_END_MARKER_US);
    }

    furi_check(idx == KIA_V3_V4_BURST_ENTRIES);
    instance->encoder.size_upload = idx;
    instance->encoder.front = 0;
}

void* subghz_protocol_encoder_kia_v3_v4_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);

    SubGhzProtocolEncoderKiaV3V4* instance = malloc(sizeof(SubGhzProtocolEncoderKiaV3V4));
    furi_check(instance);

    instance->base.protocol = &subghz_protocol_kia_v3_v4;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->serial = 0;
    instance->btn = 0;
    instance->cnt = 0;
    instance->version = 0;
    instance->mf_key = 0;
    instance->crc_iter = 0;
    instance->bursts_sent = 0;

    instance->encoder.size_upload = KIA_V3_V4_UPLOAD_CAPACITY;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    furi_check(instance->encoder.upload);
    instance->encoder.repeat = (int32_t)KIA_V3_V4_DEFAULT_REPEAT;
    instance->encoder.front = 0;
    instance->encoder.is_running = false;

    return instance;
}

void subghz_protocol_encoder_kia_v3_v4_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV3V4* instance = context;
    if(instance->encoder.upload) {
        free(instance->encoder.upload);
    }
    free(instance);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_kia_v3_v4_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV3V4* instance = context;

    instance->encoder.is_running = false;
    instance->encoder.front = 0;

    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    flipper_format_rewind(flipper_format);

    do {
        FuriString* temp_str = furi_string_alloc();
        if(!flipper_format_read_string(flipper_format, "Protocol", temp_str)) {
            FURI_LOG_E(TAG, "Missing Protocol");
            furi_string_free(temp_str);
            break;
        }

        const char* proto_str = furi_string_get_cstr(temp_str);
        if(!furi_string_equal(temp_str, instance->base.protocol->name) &&
           strcmp(proto_str, "Kia V3") != 0 && strcmp(proto_str, "Kia V4") != 0) {
            FURI_LOG_E(TAG, "Wrong protocol %s", proto_str);
            furi_string_free(temp_str);
            break;
        }

        bool version_from_protocol_name = false;

        if(strcmp(proto_str, "Kia V3") == 0) {
            instance->version = 1;
            version_from_protocol_name = true;
        } else if(strcmp(proto_str, "Kia V4") == 0) {
            instance->version = 0;
            version_from_protocol_name = true;
        }

        furi_string_free(temp_str);

        flipper_format_rewind(flipper_format);
        uint32_t bits = 0;
        if(!flipper_format_read_uint32(flipper_format, "Bit", &bits, 1)) break;
        instance->generic.data_count_bit = 68;

        flipper_format_rewind(flipper_format);
        temp_str = furi_string_alloc();
        if(!flipper_format_read_string(flipper_format, "Key", temp_str)) {
            furi_string_free(temp_str);
            break;
        }

        const char* key_str = furi_string_get_cstr(temp_str);
        uint64_t key = 0;
        size_t str_len = strlen(key_str);
        size_t hex_pos = 0;
        for(size_t i = 0; i < str_len && hex_pos < 16; i++) {
            char c = key_str[i];
            if(c == ' ') continue;
            uint8_t nibble;
            if(c >= '0' && c <= '9') {
                nibble = c - '0';
            } else if(c >= 'A' && c <= 'F') {
                nibble = c - 'A' + 10;
            } else if(c >= 'a' && c <= 'f') {
                nibble = c - 'a' + 10;
            } else {
                break;
            }
            key = (key << 4) | nibble;
            hex_pos++;
        }
        furi_string_free(temp_str);

        if(hex_pos < 14) {
            FURI_LOG_E(TAG, "Invalid key: %zu nibbles", hex_pos);
            break;
        }
        instance->generic.data = key;

        uint32_t serial = UINT32_MAX;
        uint32_t btn = UINT32_MAX;
        uint32_t cnt = UINT32_MAX;

        flipper_format_rewind(flipper_format);
        flipper_format_read_uint32(flipper_format, "Serial", &serial, 1);
        flipper_format_rewind(flipper_format);
        flipper_format_read_uint32(flipper_format, "Btn", &btn, 1);
        flipper_format_rewind(flipper_format);
        flipper_format_read_uint32(flipper_format, "Cnt", &cnt, 1);

        if(serial == UINT32_MAX || btn == UINT32_MAX || cnt == UINT32_MAX) break;
        instance->serial = serial;
        instance->btn = (uint8_t)btn;
        instance->cnt = (uint16_t)cnt;

        // Pick the manufacturer key that decoded the captured frame so re-encrypt
        // uses the key this car expects; 0 falls back to KIA_MF_KEY in build_packet.
        instance->mf_key = kia_v3_v4_select_mf_key(
            kia_v3_v4_encrypted_from_keydata(instance->generic.data),
            instance->btn,
            (uint8_t)(instance->serial & 0xFFU));

        uint32_t kia_v3_v4_txraw = 0;
        flipper_format_rewind(flipper_format);
        flipper_format_read_uint32(flipper_format, "TxRaw", &kia_v3_v4_txraw, 1);
        if(kia_v3_v4_txraw == 0) {
            uint32_t mult = furi_hal_subghz_get_rolling_counter_mult();
            if(mult == 0U) mult = 1U;
            instance->cnt = (uint16_t)((instance->cnt + mult) & 0xFFFFU);
            flipper_format_rewind(flipper_format);
            uint32_t cnt_store = instance->cnt;
            flipper_format_insert_or_update_uint32(flipper_format, "Cnt", &cnt_store, 1);
        }

        instance->generic.serial = instance->serial;
        instance->generic.btn = instance->btn;
        instance->generic.cnt = instance->cnt;

        flipper_format_rewind(flipper_format);
        uint32_t version_temp;
        if(flipper_format_read_uint32(flipper_format, "KIAVersion", &version_temp, 1)) {
            if(!version_from_protocol_name) {
                instance->version = (uint8_t)version_temp;
            }
        } else if(!version_from_protocol_name) {
            instance->version = 0;
        }

        flipper_format_rewind(flipper_format);
        uint32_t repeat_temp = KIA_V3_V4_DEFAULT_REPEAT;
        flipper_format_read_uint32(flipper_format, "Repeat", &repeat_temp, 1);
        instance->encoder.repeat = (int32_t)repeat_temp;

        instance->crc_iter = 0;
        instance->bursts_sent = 0;

        subghz_protocol_encoder_kia_v3_v4_get_upload(instance);

        instance->encoder.is_running = true;
        instance->encoder.front = 0;

        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

void subghz_protocol_encoder_kia_v3_v4_stop(void* context) {
    if(!context) return;
    SubGhzProtocolEncoderKiaV3V4* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
}

LevelDuration subghz_protocol_encoder_kia_v3_v4_yield(void* context) {
    SubGhzProtocolEncoderKiaV3V4* instance = context;

    if(!instance || !instance->encoder.upload || instance->encoder.repeat == 0 ||
       !instance->encoder.is_running) {
        if(instance) {
            instance->encoder.is_running = false;
        }
        return level_duration_reset();
    }

    if(instance->encoder.front >= instance->encoder.size_upload) {
        instance->encoder.is_running = false;
        instance->encoder.front = 0;
        return level_duration_reset();
    }

    LevelDuration ret = instance->encoder.upload[instance->encoder.front];

    if(++instance->encoder.front == instance->encoder.size_upload) {
        instance->crc_iter = (uint8_t)((instance->crc_iter + 1U) & 0x0FU);
        subghz_protocol_encoder_kia_v3_v4_patch_crc(instance);
        instance->encoder.front = 0;
        instance->encoder.repeat--;
        if(instance->bursts_sent < KIA_V3_V4_CRC_SWEEP_COUNT) {
            instance->bursts_sent++;
        }
    }

    return ret;
}

static const SubGhzProtocolEncoder enc_kia_v3_v4_vt = {
    .alloc = subghz_protocol_encoder_kia_v3_v4_alloc,
    .free = subghz_protocol_encoder_kia_v3_v4_free,
    .deserialize = subghz_protocol_encoder_kia_v3_v4_deserialize,
    .stop = subghz_protocol_encoder_kia_v3_v4_stop,
    .yield = subghz_protocol_encoder_kia_v3_v4_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_kia_v3_v4 = {
    .name = SUBGHZ_PROTOCOL_KIA_V3_V4_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = NULL,
    .encoder = &enc_kia_v3_v4_vt,
};

static const SubGhzEncodePluginEntry enc_kia_v3_v4_entry = {.name = SUBGHZ_PROTOCOL_KIA_V3_V4_NAME, .encoder = &enc_kia_v3_v4_vt, .host_slot = &enc_kia_v3_v4_host_raw};
static const FlipperAppPluginDescriptor enc_kia_v3_v4_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_kia_v3_v4_entry};
const FlipperAppPluginDescriptor* enc_kia_v3_v4_ep(void) { return &enc_kia_v3_v4_descriptor; }
