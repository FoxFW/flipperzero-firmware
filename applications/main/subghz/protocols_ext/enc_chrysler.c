/* enc_chrysler.fal : SD encoder for chrysler (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_chrysler enc_chrysler__subghz_protocol_chrysler
#define subghz_protocol_encoder_chrysler_alloc enc_chrysler__subghz_protocol_encoder_chrysler_alloc
#define subghz_protocol_encoder_chrysler_deserialize enc_chrysler__subghz_protocol_encoder_chrysler_deserialize
#define subghz_protocol_encoder_chrysler_yield enc_chrysler__subghz_protocol_encoder_chrysler_yield
#define subghz_protocol_encoder_common_free enc_chrysler__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_chrysler__subghz_protocol_encoder_common_stop
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/chrysler.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <string.h>
#include <lib/subghz/protocols/common.h>

#define CHRYSLER_V0_TE_SHORT         0x12C
#define CHRYSLER_V0_TE_LONG_A        0xD48
#define CHRYSLER_V0_TE_LONG_B        0xE74
#define CHRYSLER_V0_TE_ONE_SHORT     0x258
#define CHRYSLER_V0_FRAME_GAP        0x3CF0
#define CHRYSLER_V0_PREAMBLE_PAIRS   24U
#define CHRYSLER_V0_DECODE_BIT_COUNT 0x50
#define CHRYSLER_V0_UPLOAD_CAPACITY 0x200U

struct SubGhzProtocolEncoderChrysler {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t tx_button;
    uint8_t plain_header;

    uint8_t plain_a[9];
    uint8_t plain_b[9];

    uint16_t data_2;
    uint8_t seed;
};

static const uint8_t chrysler_v0_xor_table[16] = {
    0x0F,
    0x02,
    0x40,
    0x0C,
    0x30,
    0x0E,
    0x70,
    0x08,
    0x10,
    0x0A,
    0x50,
    0xF4,
    0x2F,
    0xF6,
    0x6F,
    0xF0,
};

static void chrysler_v0_u64_to_bytes_be(uint64_t data, uint8_t bytes[8]);
static uint64_t chrysler_v0_bytes_to_u64_be(const uint8_t bytes[8]);
static uint8_t chrysler_v0_reverse6(uint32_t value);
static void
    chrysler_v0_transform_block(const uint8_t in[9], uint8_t out[9], uint32_t key, uint8_t button);
static uint8_t chrysler_v0_payload_get_bit(const uint8_t payload[10], uint8_t index);
static void chrysler_v0_build_payload(
    const uint8_t plain[9],
    uint8_t counter,
    uint8_t button,
    uint8_t header_low2,
    uint8_t out[10]);
static size_t chrysler_v0_build_upload(
    SubGhzProtocolEncoderChrysler* instance,
    const uint8_t payload_a[10],
    const uint8_t payload_b[10]);

static void chrysler_v0_u64_to_bytes_be(uint64_t data, uint8_t bytes[8]) {
    for(size_t i = 0; i < 8; i++) {
        bytes[i] = (uint8_t)((data >> ((7U - i) * 8U)) & 0xFFU);
    }
}

static uint64_t chrysler_v0_bytes_to_u64_be(const uint8_t bytes[8]) {
    uint64_t data = 0;
    for(size_t i = 0; i < 8; i++) {
        data = (data << 8U) | bytes[i];
    }
    return data;
}

static uint8_t chrysler_v0_reverse6(uint32_t value) {
    uint8_t out = 0;
    uint8_t bits = 6;

    while(bits--) {
        out = (uint8_t)((out << 1U) | (value & 1U));
        value >>= 1U;
    }

    return out;
}

static void
    chrysler_v0_transform_block(const uint8_t in[9], uint8_t out[9], uint32_t key, uint8_t button) {
    uint8_t mask = chrysler_v0_xor_table[key & 0x0FU];
    if(button == 1U) {
        mask ^= (key & 1U) ? 0xF0U : 0x0FU;
    }

    for(size_t i = 0; i < 9; i++) {
        out[i] = in[i] ^ mask;
    }
}

static uint8_t chrysler_v0_payload_get_bit(const uint8_t payload[10], uint8_t index) {
    const uint8_t byte = payload[index >> 3U];
    const uint8_t shift = 7U - (index & 7U);
    return (byte >> shift) & 1U;
}

static void chrysler_v0_build_payload(
    const uint8_t plain[9],
    uint8_t counter,
    uint8_t button,
    uint8_t header_low2,
    uint8_t out[10]) {
    uint8_t transformed[9];
    chrysler_v0_transform_block(plain, transformed, counter, button);

    out[0] = (uint8_t)((chrysler_v0_reverse6(counter) << 2U) | (header_low2 & 0x03U));
    memcpy(&out[1], transformed, sizeof(transformed));
}

static size_t chrysler_v0_build_upload(
    SubGhzProtocolEncoderChrysler* instance,
    const uint8_t payload_a[10],
    const uint8_t payload_b[10]) {
    size_t i = 0;
    LevelDuration* upload = instance->encoder.upload;

    for(size_t preamble = 0; preamble < CHRYSLER_V0_PREAMBLE_PAIRS; preamble++) {
        upload[i++] = level_duration_make(true, CHRYSLER_V0_TE_SHORT);
        upload[i++] = level_duration_make(false, CHRYSLER_V0_TE_LONG_B);
    }

    upload[i++] = level_duration_make(true, CHRYSLER_V0_TE_SHORT);
    upload[i++] = level_duration_make(false, CHRYSLER_V0_FRAME_GAP);

    for(uint8_t bit = 0; bit < 80; bit++) {
        const bool value = chrysler_v0_payload_get_bit(payload_a, bit);
        upload[i++] = level_duration_make(true, value ? CHRYSLER_V0_TE_ONE_SHORT : CHRYSLER_V0_TE_SHORT);
        upload[i++] = level_duration_make(false, value ? CHRYSLER_V0_TE_LONG_A : CHRYSLER_V0_TE_LONG_B);
    }

    upload[i++] = level_duration_make(true, CHRYSLER_V0_TE_SHORT);
    upload[i++] = level_duration_make(false, CHRYSLER_V0_FRAME_GAP);

    for(size_t preamble = 0; preamble < CHRYSLER_V0_PREAMBLE_PAIRS; preamble++) {
        upload[i++] = level_duration_make(true, CHRYSLER_V0_TE_SHORT);
        upload[i++] = level_duration_make(false, CHRYSLER_V0_TE_LONG_B);
    }

    upload[i++] = level_duration_make(true, CHRYSLER_V0_TE_SHORT);
    upload[i++] = level_duration_make(false, CHRYSLER_V0_FRAME_GAP);

    for(uint8_t bit = 0; bit < 80; bit++) {
        const bool value = chrysler_v0_payload_get_bit(payload_b, bit);
        upload[i++] = level_duration_make(true, value ? CHRYSLER_V0_TE_ONE_SHORT : CHRYSLER_V0_TE_SHORT);
        upload[i++] = level_duration_make(false, value ? CHRYSLER_V0_TE_LONG_A : CHRYSLER_V0_TE_LONG_B);
    }

    upload[i++] = level_duration_make(true, CHRYSLER_V0_TE_SHORT);
    upload[i++] = level_duration_make(false, CHRYSLER_V0_FRAME_GAP);

    instance->encoder.size_upload = i;
    return i;
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

LevelDuration subghz_protocol_encoder_chrysler_yield(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderChrysler* instance = context;
    if(instance->encoder.repeat == 0 || !instance->encoder.is_running || instance->encoder.size_upload == 0) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }
    LevelDuration ret = instance->encoder.upload[instance->encoder.front];
    if(++instance->encoder.front == instance->encoder.size_upload) {
        instance->encoder.repeat--;
        instance->encoder.front = 0;
    }
    return ret;
}

void* subghz_protocol_encoder_chrysler_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);

    SubGhzProtocolEncoderChrysler* instance =
        calloc(1, sizeof(SubGhzProtocolEncoderChrysler));
    furi_check(instance);

    instance->base.protocol = &subghz_protocol_chrysler;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = 2;
    instance->encoder.size_upload = 0;
    instance->encoder.upload = NULL;
    instance->encoder.is_running = false;

    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_chrysler_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);

    SubGhzProtocolEncoderChrysler* instance = context;
    if(!flipper_format_rewind(flipper_format)) {
        return SubGhzProtocolStatusError;
    }
    FuriString* tmp_str = furi_string_alloc();
    bool protocol_match = false;
    if(flipper_format_read_string(flipper_format, "Protocol", tmp_str) &&
       furi_string_equal(tmp_str, instance->base.protocol->name)) {
        protocol_match = true;
    }
    furi_string_free(tmp_str);
    if(!protocol_match) {
        return SubGhzProtocolStatusError;
    }

    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, CHRYSLER_V0_DECODE_BIT_COUNT);
    if(status != SubGhzProtocolStatusOk) {
        return status;
    }

    if(!flipper_format_rewind(flipper_format)) {
        return SubGhzProtocolStatusError;
    }

    uint16_t key2 = 0U;
    if(!flipper_format_read_hex(flipper_format, "Key_2", (uint8_t*)&key2, 2)) {
        return SubGhzProtocolStatusError;
    }

    key2 = __builtin_bswap16(key2);
    instance->data_2 = key2;

    uint8_t key[8];
    chrysler_v0_u64_to_bytes_be(instance->generic.data, key);
    const uint8_t b0 = key[0];

    instance->seed = chrysler_v0_reverse6(((uint32_t)(instance->generic.data >> 56U)) >> 2U);
    instance->plain_header = (uint8_t)((instance->generic.data >> 56U) & 0x03U);

    if((b0 & 0x80U) == 0U) {
        instance->tx_button = (((uint8_t)(key[1] ^ key[6])) == 0x08U) ? 2U : 1U;
    } else {
        instance->tx_button = (((uint8_t)(key2 & 0xFFU) ^ key[4]) == 0x10U) ? 2U : 1U;
    }

    const uint8_t original_button = instance->tx_button;

    uint8_t encoded[9];
    uint8_t generated[9];
    encoded[0] = key[1];
    encoded[1] = key[2];
    encoded[2] = key[3];
    encoded[3] = key[4];
    encoded[4] = key[5];
    encoded[5] = key[6];
    encoded[6] = key[7];
    encoded[7] = (uint8_t)(key2 >> 8U);
    encoded[8] = (uint8_t)(key2 & 0xFFU);
    chrysler_v0_transform_block(encoded, generated, instance->seed, instance->tx_button);

    if(flipper_format_rewind(flipper_format) &&
       flipper_format_read_hex(flipper_format, "Plain_A", instance->plain_a, 9)) {
        if(!(flipper_format_rewind(flipper_format) &&
             flipper_format_read_hex(flipper_format, "Plain_B", instance->plain_b, 9))) {
            memcpy(instance->plain_b, instance->plain_a, sizeof(instance->plain_b));
        }
    } else if(
        flipper_format_rewind(flipper_format) &&
        flipper_format_read_hex(flipper_format, "Plain_B", instance->plain_b, 9)) {
        memcpy(instance->plain_a, instance->plain_b, sizeof(instance->plain_a));
    } else {
        memcpy(instance->plain_a, generated, sizeof(instance->plain_a));
        memcpy(instance->plain_b, generated, sizeof(instance->plain_b));
    }

    uint32_t btn_u32 = 0;
    uint32_t cnt_u32 = instance->seed & 0x3FU;
    {
        uint32_t tmp = 0;
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "Btn", &tmp, 1)) btn_u32 = tmp;
        tmp = 0;
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "Cnt", &tmp, 1)) cnt_u32 = tmp;
    }

    uint8_t tx_button = original_button;
    if(btn_u32 == 1U || btn_u32 == 2U) {
        tx_button = (uint8_t)btn_u32;
    }

    {
        if(subghz_custom_btn_get_original() == 0) {
            subghz_custom_btn_set_original(original_button);
        }
        subghz_custom_btn_set_max(2);
        uint8_t custom_btn_id = subghz_custom_btn_get();
        switch(custom_btn_id) {
        case SUBGHZ_CUSTOM_BTN_UP:
            tx_button = 0x1U;
            break;
        case SUBGHZ_CUSTOM_BTN_OK:
            tx_button = (original_button == 1U || original_button == 2U) ? original_button : 0x2U;
            break;
        default:
            break;
        }
    }

    instance->tx_button = tx_button;
    if(tx_button != original_button) {
        instance->plain_a[5] ^= 0x0CU;
        instance->plain_b[3] ^= 0x30U;
    }

    {
        flipper_format_rewind(flipper_format);
        uint32_t repeat_val = 0;
        instance->encoder.repeat =
            flipper_format_read_uint32(flipper_format, "Repeat", &repeat_val, 1) ? repeat_val : 2;
    }

    {
        uint32_t chrysler_txraw = 0;
        flipper_format_rewind(flipper_format);
        flipper_format_read_uint32(flipper_format, "TxRaw", &chrysler_txraw, 1);
        flipper_format_rewind(flipper_format);
        if(chrysler_txraw == 0) {
            uint32_t mult = furi_hal_subghz_get_rolling_counter_mult();
            if(mult == 0U) mult = 1U;
            cnt_u32 = (cnt_u32 + (2U * mult)) & 0x3FU;
        }
    }

    uint32_t counter = cnt_u32 & 0x3FU;

    uint8_t counter_a = (uint8_t)(counter & 0x3FU);
    if(counter_a & 1U) {
        counter_a = (uint8_t)((counter_a - 1U) & 0x3FU);
    }
    instance->seed = counter_a;
    const uint8_t counter_b = (counter_a == 0U) ? 0x3FU : (uint8_t)(counter_a - 1U);

    uint8_t payload_a[10];
    uint8_t payload_b[10];
    chrysler_v0_build_payload(
        instance->plain_a, counter_a, instance->tx_button, instance->plain_header, payload_a);
    chrysler_v0_build_payload(
        instance->plain_b, counter_b, instance->tx_button, instance->plain_header, payload_b);

    instance->encoder.upload = malloc(CHRYSLER_V0_UPLOAD_CAPACITY * sizeof(LevelDuration));
    furi_check(instance->encoder.upload);
    instance->encoder.size_upload = CHRYSLER_V0_UPLOAD_CAPACITY;
    chrysler_v0_build_upload(instance, payload_a, payload_b);

    instance->generic.data = chrysler_v0_bytes_to_u64_be(payload_a);
    instance->data_2 = ((uint16_t)payload_a[8] << 8U) | payload_a[9];

    if(!flipper_format_rewind(flipper_format)) {
        return SubGhzProtocolStatusError;
    }
    if(!flipper_format_update_hex(flipper_format, "Key", payload_a, 8)) {
        return SubGhzProtocolStatusError;
    }

    if(!flipper_format_rewind(flipper_format)) {
        return SubGhzProtocolStatusError;
    }

    uint16_t key2_out = __builtin_bswap16(instance->data_2);
    if(!flipper_format_update_hex(flipper_format, "Key_2", (uint8_t*)&key2_out, 2)) {
        return SubGhzProtocolStatusError;
    }

    instance->encoder.front = 0;
    instance->encoder.is_running = true;
    return status;
}

static const SubGhzProtocolEncoder enc_chrysler_vt = {
    .alloc = subghz_protocol_encoder_chrysler_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_chrysler_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_chrysler_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_chrysler = {
    .name = CHRYSLER_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = NULL,
    .encoder = &enc_chrysler_vt,
};

static const SubGhzEncodePluginEntry enc_chrysler_entry = {.name = CHRYSLER_PROTOCOL_NAME, .encoder = &enc_chrysler_vt};
static const FlipperAppPluginDescriptor enc_chrysler_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_chrysler_entry};
const FlipperAppPluginDescriptor* enc_chrysler_ep(void) { return &enc_chrysler_descriptor; }
