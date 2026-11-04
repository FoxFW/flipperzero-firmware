/* enc_kia_v7.fal : SD encoder for kia_v7 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define kia_protocol_encoder_v7_alloc enc_kia_v7__kia_protocol_encoder_v7_alloc
#define kia_protocol_encoder_v7_deserialize enc_kia_v7__kia_protocol_encoder_v7_deserialize
#define subghz_protocol_encoder_common_free enc_kia_v7__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_kia_v7__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_kia_v7__subghz_protocol_encoder_common_yield
#define subghz_protocol_kia_v7 enc_kia_v7__subghz_protocol_kia_v7
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/kia_v7.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <string.h>
#include <lib/subghz/protocols/common.h>

#define KIA_V7_UPLOAD_CAPACITY    0x3A4
#define KIA_V7_PREAMBLE_PAIRS     0x13F
#define KIA_V7_TAIL_GAP_US        0x7D0
#define KIA_V7_KEY_BITS           64U
#define KIA_V7_DEFAULT_TX_REPEAT  10U

struct SubGhzProtocolEncoderKiaV7 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t tx_bit_count;
    uint8_t decoded_button;
    uint8_t fixed_high_byte;
    uint8_t crc_calculated;
    uint8_t crc_raw;
    bool crc_valid;
};

static const SubGhzBlockConst subghz_protocol_kia_v7_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 100,
    .min_count_bit_for_found = KIA_V7_KEY_BITS,
};

static uint8_t kia_v7_crc8(const uint8_t* data, size_t len);
static void kia_v7_u64_to_bytes_be(uint64_t data, uint8_t bytes[8]);
static uint64_t kia_v7_bytes_to_u64_be(const uint8_t bytes[8]);
static void kia_v7_decode_key_common(
    SubGhzBlockGeneric* generic,
    uint8_t* decoded_button,
    uint8_t* fixed_high_byte,
    uint8_t* crc_calculated,
    uint8_t* crc_raw,
    bool* crc_valid);
static uint64_t kia_v7_encode_key(
    uint8_t fixed_high_byte,
    uint32_t serial,
    uint8_t button,
    uint16_t counter,
    uint8_t* crc_out);
static void kia_v7_decode_key_encoder(SubGhzProtocolEncoderKiaV7* instance);
static bool kia_v7_encoder_get_upload(SubGhzProtocolEncoderKiaV7* instance);

static uint8_t kia_v7_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0x4CU;

    for(size_t index = 0; index < len; index++) {
        crc ^= data[index];
        for(uint8_t bit = 0; bit < 8; bit++) {
            const bool msb = (crc & 0x80U) != 0U;
            crc <<= 1U;
            if(msb) {
                crc ^= 0x7FU;
            }
        }
    }

    return crc;
}

static void kia_v7_u64_to_bytes_be(uint64_t data, uint8_t bytes[8]) {
    for(size_t index = 0; index < 8; index++) {
        bytes[index] = (data >> ((7U - index) * 8U)) & 0xFFU;
    }
}

static uint64_t kia_v7_bytes_to_u64_be(const uint8_t bytes[8]) {
    uint64_t data = 0;

    for(size_t index = 0; index < 8; index++) {
        data = (data << 8U) | bytes[index];
    }

    return data;
}

static void kia_v7_decode_key_common(
    SubGhzBlockGeneric* generic,
    uint8_t* decoded_button,
    uint8_t* fixed_high_byte,
    uint8_t* crc_calculated,
    uint8_t* crc_raw,
    bool* crc_valid) {
    uint8_t bytes[8];
    kia_v7_u64_to_bytes_be(generic->data, bytes);

    const uint32_t serial = (((uint32_t)bytes[3]) << 20U) | (((uint32_t)bytes[4]) << 12U) |
                            (((uint32_t)bytes[5]) << 4U) | (((uint32_t)bytes[6]) >> 4U);
    const uint16_t counter = ((uint16_t)bytes[1] << 8U) | (uint16_t)bytes[2];
    const uint8_t button = bytes[6] & 0x0FU;
    const uint8_t crc_calc = kia_v7_crc8(bytes, 7);
    const uint8_t crc_pkt = bytes[7];

    generic->serial = serial & 0x0FFFFFFFU;
    generic->btn = button;
    generic->cnt = counter;
    generic->data_count_bit = KIA_V7_KEY_BITS;

    if(decoded_button) {
        *decoded_button = button;
    }
    if(fixed_high_byte) {
        *fixed_high_byte = bytes[0];
    }
    if(crc_calculated) {
        *crc_calculated = crc_calc;
    }
    if(crc_raw) {
        *crc_raw = crc_pkt;
    }
    if(crc_valid) {
        *crc_valid = (crc_calc == crc_pkt);
    }
}

static uint64_t kia_v7_encode_key(
    uint8_t fixed_high_byte,
    uint32_t serial,
    uint8_t button,
    uint16_t counter,
    uint8_t* crc_out) {
    uint8_t bytes[8];

    serial &= 0x0FFFFFFFU;
    button &= 0x0FU;

    bytes[0] = fixed_high_byte;
    bytes[1] = (counter >> 8U) & 0xFFU;
    bytes[2] = counter & 0xFFU;
    bytes[3] = (serial >> 20U) & 0xFFU;
    bytes[4] = (serial >> 12U) & 0xFFU;
    bytes[5] = (serial >> 4U) & 0xFFU;
    bytes[6] = ((serial & 0x0FU) << 4U) | button;
    bytes[7] = kia_v7_crc8(bytes, 7);

    if(crc_out) {
        *crc_out = bytes[7];
    }

    return kia_v7_bytes_to_u64_be(bytes);
}

static void kia_v7_decode_key_encoder(SubGhzProtocolEncoderKiaV7* instance) {
    kia_v7_decode_key_common(
        &instance->generic,
        &instance->decoded_button,
        &instance->fixed_high_byte,
        &instance->crc_calculated,
        &instance->crc_raw,
        &instance->crc_valid);
}

static bool kia_v7_encoder_get_upload(SubGhzProtocolEncoderKiaV7* instance) {
    furi_check(instance);

    const LevelDuration high_short =
        level_duration_make(true, subghz_protocol_kia_v7_const.te_short);
    const LevelDuration low_short =
        level_duration_make(false, subghz_protocol_kia_v7_const.te_short);
    const LevelDuration low_tail = level_duration_make(false, KIA_V7_TAIL_GAP_US);
    const size_t max_size = KIA_V7_UPLOAD_CAPACITY;

    const uint8_t bit_count = (instance->tx_bit_count > 0U && instance->tx_bit_count <= 64U) ?
                                  instance->tx_bit_count :
                                  64U;

    size_t final_size = 0;

    for(uint8_t pass = 0; pass < 2; pass++) {
        size_t index = pass;

        for(size_t i = 0; i < KIA_V7_PREAMBLE_PAIRS; i++) {
            if((index + 2U) > max_size) {
                return false;
            }

            instance->encoder.upload[index++] = high_short;
            instance->encoder.upload[index++] = low_short;
        }

        if((index + 1U) > max_size) {
            return false;
        }
        instance->encoder.upload[index++] = high_short;

        for(int32_t bit = (int32_t)bit_count - 1; bit >= 0; bit--) {
            if((index + 2U) > max_size) {
                return false;
            }

            const bool value = ((instance->generic.data >> bit) & 1ULL) != 0ULL;
            instance->encoder.upload[index++] = value ? high_short : low_short;
            instance->encoder.upload[index++] = value ? low_short : high_short;
        }

        if((index + 2U) > max_size) {
            return false;
        }
        instance->encoder.upload[index++] = high_short;
        instance->encoder.upload[index++] = low_tail;

        final_size = index;
    }

    instance->encoder.front = 0;
    instance->encoder.size_upload = final_size;
    return true;
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

void* kia_protocol_encoder_v7_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);

    SubGhzProtocolEncoderKiaV7* instance = calloc(1, sizeof(SubGhzProtocolEncoderKiaV7));
    furi_check(instance);

    instance->base.protocol = &subghz_protocol_kia_v7;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = 1;
    instance->encoder.size_upload = KIA_V7_UPLOAD_CAPACITY;
    instance->encoder.upload = malloc(KIA_V7_UPLOAD_CAPACITY * sizeof(LevelDuration));
    furi_check(instance->encoder.upload);

    return instance;
}

SubGhzProtocolStatus
    kia_protocol_encoder_v7_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);

    SubGhzProtocolEncoderKiaV7* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    instance->encoder.repeat = KIA_V7_DEFAULT_TX_REPEAT;

    do {
        FuriString* temp_str = furi_string_alloc();
        if(!temp_str) {
            break;
        }

        flipper_format_rewind(flipper_format);
        if(!flipper_format_read_string(flipper_format, "Protocol", temp_str)) {
            furi_string_free(temp_str);
            break;
        }

        if(!furi_string_equal(temp_str, instance->base.protocol->name)) {
            furi_string_free(temp_str);
            break;
        }
        furi_string_free(temp_str);

        flipper_format_rewind(flipper_format);
        SubGhzProtocolStatus load_st = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic, flipper_format, KIA_V7_KEY_BITS);
        if(load_st != SubGhzProtocolStatusOk) {
            break;
        }

        instance->tx_bit_count =
            (instance->generic.data_count_bit > 0U && instance->generic.data_count_bit <= 64U) ?
                (uint8_t)instance->generic.data_count_bit :
                64U;

        kia_v7_decode_key_encoder(instance);

        uint32_t u32 = 0;
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "Serial", &u32, 1)) {
            instance->generic.serial = u32;
        }
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "Btn", &u32, 1)) {
            instance->generic.btn = (uint8_t)u32;
        }
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "Cnt", &u32, 1)) {
            instance->generic.cnt = (uint16_t)u32;
        }

        instance->generic.btn &= 0x0FU;
        instance->generic.cnt &= 0xFFFFU;
        instance->generic.serial &= 0x0FFFFFFFU;

        uint32_t kia_v7_txraw = 0;
        flipper_format_rewind(flipper_format);
        flipper_format_read_uint32(flipper_format, "TxRaw", &kia_v7_txraw, 1);
        flipper_format_rewind(flipper_format);
        if(kia_v7_txraw == 0) {
            uint32_t mult = furi_hal_subghz_get_rolling_counter_mult();
            if(mult == 0U) mult = 1U;
            instance->generic.cnt = (instance->generic.cnt + mult) & 0xFFFFU;
        }

        instance->generic.data = kia_v7_encode_key(
            instance->fixed_high_byte,
            instance->generic.serial,
            instance->generic.btn,
            (uint16_t)instance->generic.cnt,
            &instance->crc_calculated);
        instance->generic.data_count_bit = KIA_V7_KEY_BITS;

        flipper_format_rewind(flipper_format);
        if(!flipper_format_read_uint32(flipper_format, "Repeat", &u32, 1)) {
            u32 = KIA_V7_DEFAULT_TX_REPEAT;
        }
        instance->encoder.repeat = u32;

        if(!kia_v7_encoder_get_upload(instance)) {
            break;
        }

        if(instance->encoder.size_upload == 0) {
            break;
        }

        flipper_format_rewind(flipper_format);
        uint8_t key_data[sizeof(uint64_t)];
        kia_v7_u64_to_bytes_be(instance->generic.data, key_data);
        if(!flipper_format_update_hex(flipper_format, "Key", key_data, sizeof(key_data))) {
            break;
        }

        flipper_format_rewind(flipper_format);
        uint32_t cnt_store = instance->generic.cnt;
        flipper_format_insert_or_update_uint32(flipper_format, "Cnt", &cnt_store, 1);

        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

static const SubGhzProtocolEncoder enc_kia_v7_vt = {
    .alloc = kia_protocol_encoder_v7_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = kia_protocol_encoder_v7_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_kia_v7 = {
    .name = KIA_PROTOCOL_V7_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = NULL,
    .encoder = &enc_kia_v7_vt,
};

static const SubGhzEncodePluginEntry enc_kia_v7_entry = {.name = KIA_PROTOCOL_V7_NAME, .encoder = &enc_kia_v7_vt};
static const FlipperAppPluginDescriptor enc_kia_v7_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_kia_v7_entry};
const FlipperAppPluginDescriptor* enc_kia_v7_ep(void) { return &enc_kia_v7_descriptor; }
