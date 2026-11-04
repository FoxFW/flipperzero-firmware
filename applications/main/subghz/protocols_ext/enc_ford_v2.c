/* enc_ford_v2.fal : SD encoder for ford_v2 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define ford_protocol_v2 enc_ford_v2__ford_protocol_v2
#define subghz_protocol_encoder_common_free enc_ford_v2__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_ford_v2__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_ford_v2_alloc enc_ford_v2__subghz_protocol_encoder_ford_v2_alloc
#define subghz_protocol_encoder_ford_v2_deserialize enc_ford_v2__subghz_protocol_encoder_ford_v2_deserialize
#define subghz_protocol_encoder_ford_v2_yield enc_ford_v2__subghz_protocol_encoder_ford_v2_yield
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/ford_v2.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <furi.h>
#include <string.h>
#include <lib/subghz/protocols/common.h>

#define FORD_V2_DATA_BITS              104U
#define FORD_V2_DATA_BYTES             13U
#define FORD_V2_ENC_TE_SHORT           240U
#define FORD_V2_ENC_PREAMBLE_PAIRS     70U
#define FORD_V2_ENC_BURST_COUNT        6U
#define FORD_V2_ENC_INTER_BURST_GAP_US 16000U
#define FORD_V2_ENC_SEPARATOR_ELEMS    2U
#define FORD_V2_ENC_PREAMBLE_ELEMS     (FORD_V2_ENC_PREAMBLE_PAIRS * 2U)
#define FORD_V2_ENC_DATA_ELEMS         ((FORD_V2_DATA_BITS - 1U) * 2U)
#define FORD_V2_ENC_BURST_ELEMS \
    (FORD_V2_ENC_PREAMBLE_ELEMS + FORD_V2_ENC_SEPARATOR_ELEMS + FORD_V2_ENC_DATA_ELEMS)
#define FORD_V2_ENC_UPLOAD_ELEMS \
    (FORD_V2_ENC_BURST_COUNT * FORD_V2_ENC_BURST_ELEMS + (FORD_V2_ENC_BURST_COUNT - 1U))
#define FORD_V2_ENC_SYNC_LO_US 476U
#define FORD_V2_KEY_BYTE_COUNT             8U
#define FORD_V2_TAIL_RAW_BYTE_COUNT        5U
#define FORD_V2_ENCODER_DEFAULT_REPEAT     10U

typedef struct SubGhzProtocolEncoderFordV2 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    uint64_t extra_data;
    uint8_t raw_bytes[FORD_V2_DATA_BYTES];
} 
SubGhzProtocolEncoderFordV2;

static bool ford_v2_button_is_valid(uint8_t btn);
static uint8_t ford_v2_uint8_parity(uint8_t value);
static inline void ford_v2_encoder_add_level(
    SubGhzProtocolEncoderFordV2* instance,
    bool level,
    uint32_t duration);
static void ford_v2_encoder_rebuild_raw_from_payload(SubGhzProtocolEncoderFordV2* instance);
static void ford_v2_encoder_refresh_data_from_raw(SubGhzProtocolEncoderFordV2* instance);
static inline void
    ford_v2_encoder_emit_manchester_bit(SubGhzProtocolEncoderFordV2* instance, bool bit);
static void ford_v2_encoder_emit_burst(SubGhzProtocolEncoderFordV2* instance);
static void ford_v2_encoder_build_upload(SubGhzProtocolEncoderFordV2* instance);
static void ford_v2_encoder_read_optional_tail_raw(
    SubGhzProtocolEncoderFordV2* instance,
    FlipperFormat* flipper_format);
static SubGhzProtocolStatus ford_v2_encoder_deserialize_read_header(
    SubGhzProtocolEncoderFordV2* instance,
    FlipperFormat* flipper_format,
    FuriString* temp_str);
static SubGhzProtocolStatus
    ford_v2_encoder_deserialize_validate_and_pack(SubGhzProtocolEncoderFordV2* instance);
static void ford_v2_encoder_deserialize_apply_repeat(
    SubGhzProtocolEncoderFordV2* instance,
    FlipperFormat* flipper_format);

static bool ford_v2_button_is_valid(uint8_t btn) {
    switch(btn) {
    case 0x10:
    case 0x11:
    case 0x13:
    case 0x14:
    case 0x15:
        return true;
    default:
        return false;
    }
}

static uint8_t ford_v2_uint8_parity(uint8_t value) {
    uint8_t parity = 0U;
    while(value) {
        parity ^= (value & 1U);
        value >>= 1U;
    }
    return parity;
}

static inline void ford_v2_encoder_add_level(
    SubGhzProtocolEncoderFordV2* instance,
    bool level,
    uint32_t duration) {
    size_t idx = instance->encoder.size_upload;
    if(idx > 0 && level_duration_get_level(instance->encoder.upload[idx - 1]) == level) {
        uint32_t prev = level_duration_get_duration(instance->encoder.upload[idx - 1]);
        instance->encoder.upload[idx - 1] = level_duration_make(level, prev + duration);
    } else {
        furi_check(idx < FORD_V2_ENC_UPLOAD_ELEMS);
        instance->encoder.upload[idx] = level_duration_make(level, duration);
        instance->encoder.size_upload++;
    }
}

static void ford_v2_encoder_rebuild_raw_from_payload(SubGhzProtocolEncoderFordV2* instance) {
    for(uint8_t i = 0; i < FORD_V2_KEY_BYTE_COUNT; i++) {
        instance->raw_bytes[i] = (uint8_t)(instance->generic.data >> (56U - i * 8U));
    }

    for(uint8_t i = 0; i < FORD_V2_TAIL_RAW_BYTE_COUNT; i++) {
        instance->raw_bytes[8U + i] = (uint8_t)(instance->extra_data >> (32U - i * 8U));
    }

    const uint8_t btn = instance->raw_bytes[6];
    const uint8_t k7_msb = (uint8_t)(ford_v2_uint8_parity(btn) << 7);
    instance->raw_bytes[7] = (instance->raw_bytes[7] & 0x7FU) | k7_msb;
}

static void ford_v2_encoder_refresh_data_from_raw(SubGhzProtocolEncoderFordV2* instance) {
    instance->generic.data = 0;
    for(uint8_t i = 0; i < FORD_V2_KEY_BYTE_COUNT; i++) {
        instance->generic.data = (instance->generic.data << 8) | (uint64_t)instance->raw_bytes[i];
    }
}

static inline void
    ford_v2_encoder_emit_manchester_bit(SubGhzProtocolEncoderFordV2* instance, bool bit) {
    if(bit) {
        ford_v2_encoder_add_level(instance, true, FORD_V2_ENC_TE_SHORT);
        ford_v2_encoder_add_level(instance, false, FORD_V2_ENC_TE_SHORT);
    } else {
        ford_v2_encoder_add_level(instance, false, FORD_V2_ENC_TE_SHORT);
        ford_v2_encoder_add_level(instance, true, FORD_V2_ENC_TE_SHORT);
    }
}

static void ford_v2_encoder_emit_burst(SubGhzProtocolEncoderFordV2* instance) {
    for(uint8_t i = 0; i < FORD_V2_ENC_PREAMBLE_PAIRS; i++) {
        ford_v2_encoder_add_level(instance, false, FORD_V2_ENC_TE_SHORT);
        ford_v2_encoder_add_level(instance, true, FORD_V2_ENC_TE_SHORT);
    }

    ford_v2_encoder_add_level(instance, false, FORD_V2_ENC_SYNC_LO_US);
    ford_v2_encoder_add_level(instance, true, FORD_V2_ENC_TE_SHORT);

    for(uint16_t bit_pos = 1U; bit_pos < FORD_V2_DATA_BITS; bit_pos++) {
        const uint8_t byte_idx = (uint8_t)(bit_pos / 8U);
        const uint8_t bit_idx = (uint8_t)(7U - (bit_pos % 8U));
        ford_v2_encoder_emit_manchester_bit(
            instance, ((instance->raw_bytes[byte_idx] >> bit_idx) & 1U) != 0U);
    }
}

static void ford_v2_encoder_build_upload(SubGhzProtocolEncoderFordV2* instance) {
    instance->encoder.size_upload = 0;
    instance->encoder.front = 0;

    for(uint8_t burst = 0; burst < FORD_V2_ENC_BURST_COUNT; burst++) {
        ford_v2_encoder_emit_burst(instance);

        if(burst + 1U < FORD_V2_ENC_BURST_COUNT) {
            ford_v2_encoder_add_level(instance, true, FORD_V2_ENC_INTER_BURST_GAP_US);
        }
    }
}

static void ford_v2_encoder_read_optional_tail_raw(
    SubGhzProtocolEncoderFordV2* instance,
    FlipperFormat* flipper_format) {
    instance->extra_data = 0U;
    uint8_t tail_raw[FORD_V2_TAIL_RAW_BYTE_COUNT] = {0};
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_hex(flipper_format, "TailRaw", tail_raw, sizeof(tail_raw))) {
        for(uint8_t i = 0; i < FORD_V2_TAIL_RAW_BYTE_COUNT; i++) {
            instance->extra_data = (instance->extra_data << 8) | (uint64_t)tail_raw[i];
        }
    }
}

static SubGhzProtocolStatus ford_v2_encoder_deserialize_read_header(
    SubGhzProtocolEncoderFordV2* instance,
    FlipperFormat* flipper_format,
    FuriString* temp_str) {
    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_string(flipper_format, "Protocol", temp_str)) {
        return SubGhzProtocolStatusError;
    }
    if(!furi_string_equal(temp_str, instance->base.protocol->name)) {
        return SubGhzProtocolStatusError;
    }

    SubGhzProtocolStatus g = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, FORD_V2_DATA_BITS);
    if(g != SubGhzProtocolStatusOk) {
        return g;
    }

    ford_v2_encoder_read_optional_tail_raw(instance, flipper_format);
    return SubGhzProtocolStatusOk;
}

static SubGhzProtocolStatus
    ford_v2_encoder_deserialize_validate_and_pack(SubGhzProtocolEncoderFordV2* instance) {
    ford_v2_encoder_rebuild_raw_from_payload(instance);

    {
        const uint8_t original_btn = instance->raw_bytes[6];
        if(subghz_custom_btn_get_original() == 0) {
            subghz_custom_btn_set_original(original_btn);
        }
        subghz_custom_btn_set_max(5);
        uint8_t custom_btn_id = subghz_custom_btn_get();
        uint8_t new_btn = original_btn;
        switch(custom_btn_id) {
        case SUBGHZ_CUSTOM_BTN_UP:    new_btn = 0x11U; break;
        case SUBGHZ_CUSTOM_BTN_OK:    new_btn = 0x10U; break;
        case SUBGHZ_CUSTOM_BTN_DOWN:  new_btn = 0x13U; break;
        case SUBGHZ_CUSTOM_BTN_LEFT:  new_btn = 0x14U; break;
        case SUBGHZ_CUSTOM_BTN_RIGHT: new_btn = 0x15U; break;
        default: break;
        }
        if(new_btn != original_btn) {
            instance->raw_bytes[6] = new_btn;
            const uint8_t k7_msb = (uint8_t)(ford_v2_uint8_parity(new_btn) << 7);
            instance->raw_bytes[7] = (instance->raw_bytes[7] & 0x7FU) | k7_msb;
        }
    }

    if(!ford_v2_button_is_valid(instance->raw_bytes[6])) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    ford_v2_encoder_refresh_data_from_raw(instance);
    instance->generic.btn = instance->raw_bytes[6];
    instance->generic.serial =
        ((uint32_t)instance->raw_bytes[2] << 24) | ((uint32_t)instance->raw_bytes[3] << 16) |
        ((uint32_t)instance->raw_bytes[4] << 8) | (uint32_t)instance->raw_bytes[5];
    instance->generic.cnt = (uint16_t)((((uint16_t)(instance->raw_bytes[7] & 0x7FU)) << 9) |
                                       (((uint16_t)instance->raw_bytes[8]) << 1) |
                                       ((uint16_t)(instance->raw_bytes[9] >> 7)));

    return SubGhzProtocolStatusOk;
}

static void ford_v2_encoder_deserialize_apply_repeat(
    SubGhzProtocolEncoderFordV2* instance,
    FlipperFormat* flipper_format) {
    flipper_format_rewind(flipper_format);
    uint32_t repeat = FORD_V2_ENCODER_DEFAULT_REPEAT;
    if(flipper_format_read_uint32(flipper_format, "Repeat", &repeat, 1)) {
        instance->encoder.repeat = (int32_t)repeat;
    }
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

void* subghz_protocol_encoder_ford_v2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderFordV2* instance = calloc(1, sizeof(SubGhzProtocolEncoderFordV2));
    furi_check(instance);

    instance->base.protocol = &ford_protocol_v2;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = FORD_V2_ENCODER_DEFAULT_REPEAT;
    instance->encoder.upload = calloc(FORD_V2_ENC_UPLOAD_ELEMS, sizeof(LevelDuration));
    furi_check(instance->encoder.upload);

    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_ford_v2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderFordV2* instance = context;

    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    instance->encoder.repeat = FORD_V2_ENCODER_DEFAULT_REPEAT;
    instance->generic.data_count_bit = FORD_V2_DATA_BITS;

    FuriString* temp_str = furi_string_alloc();
    furi_check(temp_str);

    SubGhzProtocolStatus ret =
        ford_v2_encoder_deserialize_read_header(instance, flipper_format, temp_str);

    if(ret == SubGhzProtocolStatusOk) {
        ret = ford_v2_encoder_deserialize_validate_and_pack(instance);
    }

    if(ret == SubGhzProtocolStatusOk) {
        ford_v2_encoder_deserialize_apply_repeat(instance, flipper_format);
        ford_v2_encoder_build_upload(instance);
        instance->encoder.is_running = true;
    }

    furi_string_free(temp_str);
    return ret;
}

LevelDuration subghz_protocol_encoder_ford_v2_yield(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderFordV2* instance = context;

    if(!instance->encoder.is_running || instance->encoder.repeat == 0U) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }

    LevelDuration ret = instance->encoder.upload[instance->encoder.front];

    if(++instance->encoder.front == instance->encoder.size_upload) {
        instance->encoder.front = 0U;
        instance->encoder.repeat--;
    }

    return ret;
}

static const SubGhzProtocolEncoder enc_ford_v2_vt = {
    .alloc = subghz_protocol_encoder_ford_v2_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_ford_v2_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_ford_v2_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol ford_protocol_v2 = {
    .name = FORD_PROTOCOL_V2_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_ford_v2_vt,
};

static const SubGhzEncodePluginEntry enc_ford_v2_entry = {.name = FORD_PROTOCOL_V2_NAME, .encoder = &enc_ford_v2_vt};
static const FlipperAppPluginDescriptor enc_ford_v2_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_ford_v2_entry};
const FlipperAppPluginDescriptor* enc_ford_v2_ep(void) { return &enc_ford_v2_descriptor; }
