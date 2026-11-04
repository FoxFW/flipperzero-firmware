/* enc_fiat_v2.fal : SD encoder for fiat_v2 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define fiat_v2_protocol enc_fiat_v2__fiat_v2_protocol
#define subghz_protocol_encoder_common_free enc_fiat_v2__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_yield enc_fiat_v2__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_fiat_v2_alloc enc_fiat_v2__subghz_protocol_encoder_fiat_v2_alloc
#define subghz_protocol_encoder_fiat_v2_deserialize enc_fiat_v2__subghz_protocol_encoder_fiat_v2_deserialize
#define subghz_protocol_encoder_fiat_v2_stop enc_fiat_v2__subghz_protocol_encoder_fiat_v2_stop
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/fiat_v2.h>
#include <lib/subghz/protocols/fiat_v1.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <string.h>
#include <lib/subghz/protocols/common.h>

#define TAG "FiatProtocolV2"
#define FIAT_V2_TE_SHORT          210U
#define FIAT_V2_TE_LONG           420U
#define FIAT_V2_TE_DELTA          100U
#define FIAT_V2_WIRE_BITS         112U
#define FIAT_V2_WIRE_BYTES        14U
#define FIAT_V2_LOGICAL_BITS      112U
#define FIAT_V2_MARKER0           0x00U
#define FIAT_V2_MARKER1           0x01U
#define FIAT_V2_BTN_SHIFT         6U
#define FIAT_V2_BUTTON_LOCK       0x2U
#define FIAT_V2_BUTTON_UNLOCK     0x3U
#define FIAT_V2_BUTTON_TRUNK      0x1U
#define FIAT_V2_CNT_SHIFT         3U
#define FIAT_V2_FCA_TYPE_NIBBLE   0xD0U
#define FIAT_V2_RAW_FIELD         "Raw"
#define FIAT_V2_HITAG2_KEY_FIELD   "Hitag2 Key"
#define FIAT_V2_HITAG2_IV_FIELD    "Hitag2 IV"
#define FIAT_V2_ENC_LEAD_US        2033U
#define FIAT_V2_ENC_GAP_US         3252U
#define FIAT_V2_ENC_DEFAULT_REPEAT 6U
#define FIAT_V2_UPLOAD_CAPACITY   (1U + (FIAT_V2_WIRE_BITS * 2U) + 1U)

typedef struct SubGhzProtocolEncoderFiatV2 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t raw_data[FIAT_V2_WIRE_BYTES];
    uint8_t hitag2_key[6];
    uint32_t epoch;
    uint8_t iv_combo;
} 
SubGhzProtocolEncoderFiatV2;

static const SubGhzBlockConst subghz_protocol_fiat_v2_const = {
    .te_short = FIAT_V2_TE_SHORT,
    .te_long = FIAT_V2_TE_LONG,
    .te_delta = FIAT_V2_TE_DELTA,
    .min_count_bit_for_found = FIAT_V2_LOGICAL_BITS,
};

/* host bridge: live firmware functions/state this encoder needs (set by the loader) */
typedef struct {
    __typeof__(&subghz_protocol_fiat_v1_compute_auth) subghz_protocol_fiat_v1_compute_auth;
    __typeof__(&subghz_protocol_fiat_v1_get_known_keys) subghz_protocol_fiat_v1_get_known_keys;
} enc_fiat_v2_host_t;
static const void* enc_fiat_v2_host_raw;
#define ENC_FIAT_V2_HOST ((const enc_fiat_v2_host_t*)enc_fiat_v2_host_raw)
#define subghz_protocol_fiat_v1_compute_auth (ENC_FIAT_V2_HOST->subghz_protocol_fiat_v1_compute_auth)
#define subghz_protocol_fiat_v1_get_known_keys (ENC_FIAT_V2_HOST->subghz_protocol_fiat_v1_get_known_keys)

static bool fiat_v2_encoder_build_upload(SubGhzProtocolEncoderFiatV2* instance);
static uint8_t fiat_v2_dpad_selector(uint8_t custom_btn_id, uint8_t original_selector);
static bool fiat_v2_button_valid(uint8_t button);
static uint32_t fiat_v2_uid(const uint8_t raw[FIAT_V2_WIRE_BYTES]);
static bool fiat_v2_is_fca(const uint8_t raw[FIAT_V2_WIRE_BYTES]);
static uint32_t fiat_v2_hop(const uint8_t raw[FIAT_V2_WIRE_BYTES]);
static uint32_t fiat_v2_counter(const uint8_t raw[FIAT_V2_WIRE_BYTES]);
static uint8_t fiat_v2_iv_button(const uint8_t raw[FIAT_V2_WIRE_BYTES], uint8_t combo);
static uint16_t fiat_v2_iv_control(const uint8_t raw[FIAT_V2_WIRE_BYTES], uint8_t combo);
static bool fiat_v2_key_matches_combo(
    const uint8_t raw[FIAT_V2_WIRE_BYTES],
    const uint8_t key[6],
    uint8_t combo);
static void fiat_v2_patch_hop(uint8_t raw[FIAT_V2_WIRE_BYTES], uint32_t hop);
static void fiat_v2_patch_button(uint8_t raw[FIAT_V2_WIRE_BYTES], uint8_t selector);
static void fiat_v2_patch_counter(uint8_t raw[FIAT_V2_WIRE_BYTES], uint32_t counter);
static bool fiat_v2_frame_valid(const uint8_t raw[FIAT_V2_WIRE_BYTES]);

static bool fiat_v2_encoder_build_upload(SubGhzProtocolEncoderFiatV2* instance) {
    furi_check(instance);
    LevelDuration* upload = instance->encoder.upload;
    if(!upload) {
        return false;
    }

    size_t index = 0U;

    upload[index++] = level_duration_make(true, FIAT_V2_ENC_LEAD_US);

    for(uint8_t bit_index = 0U; bit_index < FIAT_V2_WIRE_BITS; bit_index++) {
        const bool bit =
            ((instance->raw_data[bit_index >> 3U] >> (7U - (bit_index & 7U))) & 1U) != 0U;
        upload[index++] = level_duration_make(bit, FIAT_V2_TE_SHORT);
        upload[index++] = level_duration_make(!bit, FIAT_V2_TE_SHORT);
    }

    upload[index++] = level_duration_make(false, FIAT_V2_ENC_GAP_US);
    instance->encoder.size_upload = index;
    instance->encoder.front = 0U;
    return true;
}

static uint8_t fiat_v2_dpad_selector(uint8_t custom_btn_id, uint8_t original_selector) {
    switch(custom_btn_id) {
    case SUBGHZ_CUSTOM_BTN_UP:
        return FIAT_V2_BUTTON_UNLOCK;
    case SUBGHZ_CUSTOM_BTN_DOWN:
        return FIAT_V2_BUTTON_LOCK;
    case SUBGHZ_CUSTOM_BTN_LEFT:
        return FIAT_V2_BUTTON_TRUNK;
    case SUBGHZ_CUSTOM_BTN_RIGHT:
    case SUBGHZ_CUSTOM_BTN_OK:
    default:
        return original_selector;
    }
}

static bool fiat_v2_button_valid(uint8_t button) {
    const uint8_t sel = button >> FIAT_V2_BTN_SHIFT;
    return sel == FIAT_V2_BUTTON_LOCK || sel == FIAT_V2_BUTTON_UNLOCK ||
           sel == FIAT_V2_BUTTON_TRUNK;
}

static uint32_t fiat_v2_uid(const uint8_t raw[FIAT_V2_WIRE_BYTES]) {
    return ((uint32_t)raw[2] << 24U) | ((uint32_t)raw[3] << 16U) |
           ((uint32_t)raw[4] << 8U) | raw[5];
}

static bool fiat_v2_is_fca(const uint8_t raw[FIAT_V2_WIRE_BYTES]) {
    return (raw[6] & 0xF0U) == FIAT_V2_FCA_TYPE_NIBBLE;
}

static uint32_t fiat_v2_hop(const uint8_t raw[FIAT_V2_WIRE_BYTES]) {
    if(fiat_v2_is_fca(raw)) {
        return ((uint32_t)raw[10] << 24U) | ((uint32_t)raw[11] << 16U) |
               ((uint32_t)raw[12] << 8U) | raw[13];
    }
    return ((uint32_t)raw[9] << 24U) | ((uint32_t)raw[10] << 16U) |
           ((uint32_t)raw[11] << 8U) | raw[12];
}

static uint32_t fiat_v2_counter(const uint8_t raw[FIAT_V2_WIRE_BYTES]) {
    if(fiat_v2_is_fca(raw)) {
        const uint32_t raw_cnt = ((uint32_t)raw[8] << 6U) | (uint32_t)(raw[9] >> 2U);
        return (~raw_cnt) & 0x3FFFU;
    }
    const uint32_t raw_cnt =
        ((uint32_t)(raw[7] & 0x3FU) << 5U) | (uint32_t)(raw[8] >> FIAT_V2_CNT_SHIFT);
    return (~raw_cnt) & 0x7FFU;
}

static uint8_t fiat_v2_iv_button(const uint8_t raw[FIAT_V2_WIRE_BYTES], uint8_t combo) {
    const uint8_t sel = (uint8_t)((raw[7] >> FIAT_V2_BTN_SHIFT) & 0x0FU);
    if((combo & 0x01U) == 0U) {
        return sel;
    }
    switch(sel) {
    case FIAT_V2_BUTTON_TRUNK:
        return 0x2U;
    case FIAT_V2_BUTTON_LOCK:
        return 0x4U;
    case FIAT_V2_BUTTON_UNLOCK:
        return 0x8U;
    default:
        return 0x0U;
    }
}

static uint16_t fiat_v2_iv_control(const uint8_t raw[FIAT_V2_WIRE_BYTES], uint8_t combo) {
    const uint16_t cnt = (uint16_t)(fiat_v2_counter(raw) & 0x3FFU);
    if((combo & 0x02U) == 0U) {
        return cnt;
    }
    return (uint16_t)((~cnt) & 0x3FFU);
}

static bool fiat_v2_key_matches_combo(
    const uint8_t raw[FIAT_V2_WIRE_BYTES],
    const uint8_t key[6],
    uint8_t combo) {
    const uint32_t uid = fiat_v2_uid(raw);
    const uint8_t btn = fiat_v2_iv_button(raw, combo);
    const uint16_t ctrl = fiat_v2_iv_control(raw, combo);
    const uint32_t hop = fiat_v2_hop(raw);
    return subghz_protocol_fiat_v1_compute_auth(uid, btn, ctrl, key, 0U) == hop;
}

static void fiat_v2_patch_hop(uint8_t raw[FIAT_V2_WIRE_BYTES], uint32_t hop) {
    if(fiat_v2_is_fca(raw)) {
        raw[10] = (uint8_t)(hop >> 24U);
        raw[11] = (uint8_t)(hop >> 16U);
        raw[12] = (uint8_t)(hop >> 8U);
        raw[13] = (uint8_t)hop;
    } else {
        raw[9] = (uint8_t)(hop >> 24U);
        raw[10] = (uint8_t)(hop >> 16U);
        raw[11] = (uint8_t)(hop >> 8U);
        raw[12] = (uint8_t)hop;
    }
}

static void fiat_v2_patch_button(uint8_t raw[FIAT_V2_WIRE_BYTES], uint8_t selector) {
    raw[7] = (uint8_t)((raw[7] & 0x3FU) | ((selector & 0x03U) << FIAT_V2_BTN_SHIFT));
}

static void fiat_v2_patch_counter(uint8_t raw[FIAT_V2_WIRE_BYTES], uint32_t counter) {
    if(fiat_v2_is_fca(raw)) {
        const uint32_t raw_cnt = (~counter) & 0x3FFFU;
        raw[8] = (uint8_t)((raw_cnt >> 6U) & 0xFFU);
        raw[9] = (uint8_t)((raw[9] & 0x03U) | ((raw_cnt & 0x3FU) << 2U));
    } else {
        const uint32_t raw_cnt = (~counter) & 0x7FFU;
        raw[7] = (uint8_t)((raw[7] & 0xC0U) | ((raw_cnt >> 5U) & 0x3FU));
        raw[8] = (uint8_t)((raw[8] & 0x07U) | ((raw_cnt & 0x1FU) << FIAT_V2_CNT_SHIFT));
    }
}

static bool fiat_v2_frame_valid(const uint8_t raw[FIAT_V2_WIRE_BYTES]) {
    if(raw[0] != FIAT_V2_MARKER0 || raw[1] != FIAT_V2_MARKER1) {
        return false;
    }
    if(!fiat_v2_button_valid(raw[7])) {
        return false;
    }

    const uint32_t uid = fiat_v2_uid(raw);
    return uid != 0U && uid != UINT32_MAX;
}

/* vendored from lib/subghz/protocols/common.c (not exported) */
void subghz_protocol_encoder_common_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderCommon* instance = context;
    free(instance->encoder.upload);
    free(instance);
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

void* subghz_protocol_encoder_fiat_v2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderFiatV2* instance = calloc(1, sizeof(SubGhzProtocolEncoderFiatV2));
    furi_check(instance);

    instance->base.protocol = &fiat_v2_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = FIAT_V2_ENC_DEFAULT_REPEAT;
    instance->encoder.upload = malloc(FIAT_V2_UPLOAD_CAPACITY * sizeof(LevelDuration));
    furi_check(instance->encoder.upload);

    return instance;
}

void subghz_protocol_encoder_fiat_v2_stop(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderFiatV2* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_fiat_v2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    furi_check(flipper_format);
    SubGhzProtocolEncoderFiatV2* instance = context;

    instance->encoder.is_running = false;
    instance->encoder.front = 0U;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_fiat_v2_const.min_count_bit_for_found);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }
    if(instance->generic.data_count_bit != FIAT_V2_LOGICAL_BITS) {
        return SubGhzProtocolStatusErrorValueBitCount;
    }

    flipper_format_rewind(flipper_format);
    uint8_t raw_tmp[FIAT_V2_WIRE_BYTES] = {0};
    if(!flipper_format_read_hex(flipper_format, FIAT_V2_RAW_FIELD, raw_tmp, sizeof(raw_tmp)) ||
       !fiat_v2_frame_valid(raw_tmp)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    memcpy(instance->raw_data, raw_tmp, sizeof(raw_tmp));

    instance->generic.serial = fiat_v2_uid(instance->raw_data);
    instance->generic.btn = instance->raw_data[7];
    instance->generic.cnt = fiat_v2_counter(instance->raw_data);
    instance->epoch = 0U;

    uint32_t override_cnt = 0U;
    bool got_cnt = false;
    {
        uint32_t tmp = 0U;
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "Serial", &tmp, 1)) {
            instance->generic.serial = tmp;
        }
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "Cnt", &tmp, 1)) {
            override_cnt = tmp;
            got_cnt = true;
            instance->generic.cnt = tmp;
        }
    }

    const uint8_t original_selector =
        (uint8_t)((instance->raw_data[7] >> FIAT_V2_BTN_SHIFT) & 0x03U);
    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(original_selector);
    }
    subghz_custom_btn_set_max(3);
    const uint8_t custom_btn_id = subghz_custom_btn_get();

    uint8_t key[6] = {0};
    bool have_key = false;
    uint8_t combo = 0U;

    flipper_format_rewind(flipper_format);
    if(flipper_format_read_hex(flipper_format, FIAT_V2_HITAG2_KEY_FIELD, key, 6U)) {
        uint32_t iv_combo = 0U;
        flipper_format_rewind(flipper_format);
        if(!flipper_format_read_uint32(flipper_format, FIAT_V2_HITAG2_IV_FIELD, &iv_combo, 1U)) {
            iv_combo = 0U;
        }
        combo = (uint8_t)(iv_combo & 0x03U);
        if(fiat_v2_key_matches_combo(instance->raw_data, key, combo)) {
            have_key = true;
        }
    }

    if(!have_key) {
        const uint8_t(*known_keys)[6] = subghz_protocol_fiat_v1_get_known_keys();
        for(uint8_t i = 0U; !have_key && i < FIAT_V1_KNOWN_KEY_COUNT; i++) {
            for(uint8_t c = 0U; c < FIAT_V2_IV_COMBO_COUNT; c++) {
                if(fiat_v2_key_matches_combo(instance->raw_data, known_keys[i], c)) {
                    memcpy(key, known_keys[i], 6U);
                    combo = c;
                    have_key = true;
                    break;
                }
            }
        }
    }

    if(have_key) {
        memcpy(instance->hitag2_key, key, 6U);
        instance->iv_combo = combo;

        const uint8_t new_selector =
            fiat_v2_dpad_selector(custom_btn_id, original_selector);

        uint32_t new_counter;
        if(got_cnt) {
            new_counter = override_cnt;
        } else {
            uint32_t mult = furi_hal_subghz_get_rolling_counter_mult();
            if(mult == 0U) mult = 1U;
            new_counter = instance->generic.cnt + mult;
        }

        fiat_v2_patch_button(instance->raw_data, new_selector);
        fiat_v2_patch_counter(instance->raw_data, new_counter);

        instance->generic.btn = instance->raw_data[7];
        instance->generic.cnt = fiat_v2_counter(instance->raw_data);

        const uint32_t uid = fiat_v2_uid(instance->raw_data);
        const uint8_t iv_btn = fiat_v2_iv_button(instance->raw_data, combo);
        const uint16_t iv_ctrl = fiat_v2_iv_control(instance->raw_data, combo);
        const uint32_t hop =
            subghz_protocol_fiat_v1_compute_auth(uid, iv_btn, iv_ctrl, key, 0U);
        fiat_v2_patch_hop(instance->raw_data, hop);

        FURI_LOG_I(
            TAG,
            "TX(hop-recompute) UID:%08lX Combo:%u Sel:%u Cnt:%04lX Hop:%08lX",
            (unsigned long)uid,
            (unsigned)combo,
            (unsigned)new_selector,
            (unsigned long)instance->generic.cnt,
            (unsigned long)hop);
    } else {
        instance->generic.cnt = fiat_v2_counter(instance->raw_data);
        instance->generic.btn = instance->raw_data[7];
        FURI_LOG_I(
            TAG,
            "TX(replay) UID:%08lX (no key, replaying captured frame)",
            (unsigned long)instance->generic.serial);
    }

    instance->generic.data =
        ((uint64_t)instance->generic.serial << 32U) | fiat_v2_hop(instance->raw_data);

    if(have_key) {
        flipper_format_rewind(flipper_format);
        flipper_format_insert_or_update_hex(
            flipper_format, FIAT_V2_RAW_FIELD, instance->raw_data, FIAT_V2_WIRE_BYTES);
        flipper_format_rewind(flipper_format);
        uint32_t cnt_store = instance->generic.cnt;
        flipper_format_insert_or_update_uint32(flipper_format, "Cnt", &cnt_store, 1);
    }

    uint32_t repeat = FIAT_V2_ENC_DEFAULT_REPEAT;
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Repeat", &repeat, 1);
    instance->encoder.repeat = (repeat == 0U) ? FIAT_V2_ENC_DEFAULT_REPEAT : (size_t)repeat;

    if(!fiat_v2_encoder_build_upload(instance)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    instance->encoder.is_running = true;

    return SubGhzProtocolStatusOk;
}

static const SubGhzProtocolEncoder enc_fiat_v2_vt = {
    .alloc = subghz_protocol_encoder_fiat_v2_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_fiat_v2_deserialize,
    .stop = subghz_protocol_encoder_fiat_v2_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol fiat_v2_protocol = {
    .name = FIAT_V2_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_fiat_v2_vt,
};

static const SubGhzEncodePluginEntry enc_fiat_v2_entry = {.name = FIAT_V2_PROTOCOL_NAME, .encoder = &enc_fiat_v2_vt, .host_slot = &enc_fiat_v2_host_raw};
static const FlipperAppPluginDescriptor enc_fiat_v2_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_fiat_v2_entry};
const FlipperAppPluginDescriptor* enc_fiat_v2_ep(void) { return &enc_fiat_v2_descriptor; }
