/* enc_honda_static.fal : SD encoder for honda_static (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define honda_static_protocol enc_honda_static__honda_static_protocol
#define subghz_protocol_encoder_common_free enc_honda_static__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_honda_static__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_honda_static_alloc enc_honda_static__subghz_protocol_encoder_honda_static_alloc
#define subghz_protocol_encoder_honda_static_deserialize enc_honda_static__subghz_protocol_encoder_honda_static_deserialize
#define subghz_protocol_encoder_honda_static_yield enc_honda_static__subghz_protocol_encoder_honda_static_yield
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/honda_static.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <string.h>
#include <lib/toolbox/level_duration.h>
#include <lib/subghz/protocols/common.h>

#define HONDA_STATIC_BIT_COUNT       64
#define HONDA_STATIC_SYNC_TIME_US    700
#define HONDA_STATIC_ELEMENT_TIME_US 63
#define HONDA_STATIC_UPLOAD_CAPACITY \
    (1U + HONDA_STATIC_PREAMBLE_ALTERNATING_COUNT + (2U * HONDA_STATIC_BIT_COUNT) + 1U)
#define HONDA_STATIC_PREAMBLE_ALTERNATING_COUNT 160

typedef struct {
    uint8_t button;
    uint32_t serial;
    uint32_t counter;
    uint8_t checksum;
} HondaStaticFields;

struct SubGhzProtocolEncoderHondaStatic {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    HondaStaticFields decoded;
    uint8_t tx_button;
};

static const uint8_t honda_static_encoder_button_map[4] = {0x02, 0x04, 0x08, 0x05};

static void honda_static_u64_to_bytes_be(uint64_t data, uint8_t bytes[8]);
static uint64_t honda_static_bytes_to_u64_be(const uint8_t bytes[8]);
static void honda_static_set_bits(uint8_t* data, uint8_t start, uint8_t count, uint32_t value);
static bool honda_static_is_valid_button(uint8_t button);
static uint8_t honda_static_encoder_remap_button(uint8_t button);
static uint8_t honda_static_compact_bytes_checksum(const uint8_t compact[8]);
static void honda_static_unpack_compact(uint64_t key, HondaStaticFields* fields);
static uint64_t honda_static_pack_compact(const HondaStaticFields* fields);
static void honda_static_build_packet_bytes(const HondaStaticFields* fields, uint8_t packet[8]);
static void honda_static_build_upload(SubGhzProtocolEncoderHondaStatic* instance);

static void honda_static_u64_to_bytes_be(uint64_t data, uint8_t bytes[8]) {
    for(size_t index = 0; index < 8; index++) {
        bytes[index] = (data >> ((7U - index) * 8U)) & 0xFFU;
    }
}

static uint64_t honda_static_bytes_to_u64_be(const uint8_t bytes[8]) {
    uint64_t data = 0;
    for(size_t index = 0; index < 8; index++) {
        data = (data << 8U) | bytes[index];
    }
    return data;
}

static void honda_static_set_bits(uint8_t* data, uint8_t start, uint8_t count, uint32_t value) {
    for(uint8_t i = 0; i < count; i++) {
        const uint8_t bit_index = start + i;
        const uint8_t byte_index = bit_index >> 3U;
        const uint8_t shift = ((uint8_t)~bit_index) & 0x07U;
        const uint8_t mask = (uint8_t)(1U << shift);
        const bool bit = ((value >> (count - 1U - i)) & 1U) != 0U;
        if(bit) {
            data[byte_index] |= mask;
        } else {
            data[byte_index] &= (uint8_t)~mask;
        }
    }
}

static bool honda_static_is_valid_button(uint8_t button) {
    if(button > 9U) return false;
    return ((0x336U >> button) & 1U) != 0U;
}

static uint8_t honda_static_encoder_remap_button(uint8_t button) {
    if(button < 2U) return 1U;
    button -= 2U;
    if(button <= 3U) return honda_static_encoder_button_map[button];
    return 1U;
}

static uint8_t honda_static_compact_bytes_checksum(const uint8_t compact[8]) {
    const uint8_t canonical[7] = {
        (uint8_t)((compact[0] << 4U) | (compact[1] >> 4U)),
        (uint8_t)((compact[1] << 4U) | (compact[2] >> 4U)),
        (uint8_t)((compact[2] << 4U) | (compact[3] >> 4U)),
        (uint8_t)((compact[3] << 4U) | (compact[4] >> 4U)),
        compact[5],
        compact[6],
        compact[7],
    };
    uint8_t checksum = 0U;
    for(size_t i = 0; i < COUNT_OF(canonical); i++) {
        checksum ^= canonical[i];
    }
    return checksum;
}

static void honda_static_unpack_compact(uint64_t key, HondaStaticFields* fields) {
    uint8_t compact[8];
    honda_static_u64_to_bytes_be(key, compact);

    memset(fields, 0, sizeof(*fields));
    fields->button = compact[0] & 0x0FU;
    fields->serial = ((uint32_t)compact[1] << 20U) | ((uint32_t)compact[2] << 12U) |
                     ((uint32_t)compact[3] << 4U) | ((uint32_t)compact[4] >> 4U);
    fields->counter = ((uint32_t)compact[5] << 16U) | ((uint32_t)compact[6] << 8U) |
                      (uint32_t)compact[7];
    fields->checksum = honda_static_compact_bytes_checksum(compact);
}

static uint64_t honda_static_pack_compact(const HondaStaticFields* fields) {
    uint8_t compact[8];
    compact[0] = fields->button & 0x0FU;
    compact[1] = (uint8_t)(fields->serial >> 20U);
    compact[2] = (uint8_t)(fields->serial >> 12U);
    compact[3] = (uint8_t)(fields->serial >> 4U);
    compact[4] = (uint8_t)(fields->serial << 4U);
    compact[5] = (uint8_t)(fields->counter >> 16U);
    compact[6] = (uint8_t)(fields->counter >> 8U);
    compact[7] = (uint8_t)fields->counter;
    return honda_static_bytes_to_u64_be(compact);
}

static void honda_static_build_packet_bytes(const HondaStaticFields* fields, uint8_t packet[8]) {
    memset(packet, 0, 8);
    honda_static_set_bits(packet, 0, 4, fields->button & 0x0FU);
    honda_static_set_bits(packet, 4, 28, fields->serial);
    honda_static_set_bits(packet, 32, 24, fields->counter);
    uint8_t checksum = 0U;
    for(size_t i = 0; i < 7; i++) {
        checksum ^= packet[i];
    }
    honda_static_set_bits(packet, 56, 8, checksum);
}

static void honda_static_build_upload(SubGhzProtocolEncoderHondaStatic* instance) {
    uint8_t packet[8];
    honda_static_build_packet_bytes(&instance->decoded, packet);

    size_t index = 0U;
    LevelDuration* up = instance->encoder.upload;
    const size_t cap = HONDA_STATIC_UPLOAD_CAPACITY;

    if(index < cap) {
        up[index++] = level_duration_make(true, HONDA_STATIC_SYNC_TIME_US);
    }

    for(size_t i = 0; i < HONDA_STATIC_PREAMBLE_ALTERNATING_COUNT; i++) {
        if(index < cap) {
            up[index++] = level_duration_make((i & 1U) != 0U, HONDA_STATIC_ELEMENT_TIME_US);
        }
    }

    for(uint8_t bit = 0U; bit < HONDA_STATIC_BIT_COUNT; bit++) {
        const bool value = ((packet[bit >> 3U] >> (((uint8_t)~bit) & 0x07U)) & 1U) != 0U;
        if(index < cap) {
            up[index++] = level_duration_make(!value, HONDA_STATIC_ELEMENT_TIME_US);
        }
        if(index < cap) {
            up[index++] = level_duration_make(value, HONDA_STATIC_ELEMENT_TIME_US);
        }
    }

    const bool last_bit = (packet[7] & 1U) != 0U;
    if(index < cap) {
        up[index++] = level_duration_make(!last_bit, HONDA_STATIC_SYNC_TIME_US);
    }

    instance->encoder.front = 0U;
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

void* subghz_protocol_encoder_honda_static_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);

    SubGhzProtocolEncoderHondaStatic* instance = malloc(sizeof(SubGhzProtocolEncoderHondaStatic));
    furi_check(instance);
    memset(instance, 0, sizeof(*instance));

    instance->base.protocol = &honda_static_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = 3U;
    instance->encoder.size_upload = HONDA_STATIC_UPLOAD_CAPACITY;
    instance->encoder.upload = malloc(HONDA_STATIC_UPLOAD_CAPACITY * sizeof(LevelDuration));
    furi_check(instance->encoder.upload);

    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_honda_static_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);

    SubGhzProtocolEncoderHondaStatic* instance = context;

    instance->encoder.is_running = false;
    instance->encoder.front = 0U;

    FuriString* temp_str = furi_string_alloc();
    if(!temp_str) return SubGhzProtocolStatusError;

    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_string(flipper_format, "Protocol", temp_str)) {
        furi_string_free(temp_str);
        return SubGhzProtocolStatusError;
    }
    if(!furi_string_equal(temp_str, instance->base.protocol->name)) {
        furi_string_free(temp_str);
        return SubGhzProtocolStatusError;
    }
    furi_string_free(temp_str);

    uint64_t key = 0;
    uint8_t key_bytes[8] = {0};
    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(flipper_format, "Key", key_bytes, 8)) {
        return SubGhzProtocolStatusError;
    }
    key = honda_static_bytes_to_u64_be(key_bytes);
    honda_static_unpack_compact(key, &instance->decoded);

    uint32_t serial = instance->decoded.serial;
    uint32_t btn_u32 = instance->decoded.button;
    uint32_t cnt = instance->decoded.counter & 0x00FFFFFFU;

    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Serial", &serial, 1);
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Btn", &btn_u32, 1);
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Cnt", &cnt, 1);

    instance->decoded.serial = serial;
    uint8_t b = (uint8_t)btn_u32;
    if(honda_static_is_valid_button(b)) {
        instance->decoded.button = b;
    } else if(b >= 2U && b <= 5U) {
        instance->decoded.button = honda_static_encoder_remap_button(b);
    }
    instance->decoded.counter = cnt & 0x00FFFFFFU;

    instance->generic.serial = instance->decoded.serial;
    instance->generic.cnt = instance->decoded.counter;
    instance->generic.btn = instance->decoded.button;
    instance->generic.data_count_bit = HONDA_STATIC_BIT_COUNT;
    instance->generic.data = honda_static_pack_compact(&instance->decoded);

    honda_static_u64_to_bytes_be(instance->generic.data, key_bytes);
    flipper_format_rewind(flipper_format);
    if(!flipper_format_update_hex(flipper_format, "Key", key_bytes, sizeof(key_bytes))) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    flipper_format_rewind(flipper_format);
    uint32_t repeat = 3U;
    flipper_format_read_uint32(flipper_format, "Repeat", &repeat, 1);
    instance->encoder.repeat = (size_t)repeat;

    honda_static_build_upload(instance);
    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

LevelDuration subghz_protocol_encoder_honda_static_yield(void* context) {
    SubGhzProtocolEncoderHondaStatic* instance = context;

    if(instance->encoder.repeat == 0 || !instance->encoder.is_running) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }

    LevelDuration duration = instance->encoder.upload[instance->encoder.front];

    if(++instance->encoder.front == instance->encoder.size_upload) {
        instance->encoder.repeat--;
        instance->encoder.front = 0;
    }

    return duration;
}

static const SubGhzProtocolEncoder enc_honda_static_vt = {
    .alloc = subghz_protocol_encoder_honda_static_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_honda_static_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_honda_static_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol honda_static_protocol = {
    .name = HONDA_STATIC_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 |
            SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_honda_static_vt,
};

static const SubGhzEncodePluginEntry enc_honda_static_entry = {.name = HONDA_STATIC_PROTOCOL_NAME, .encoder = &enc_honda_static_vt};
static const FlipperAppPluginDescriptor enc_honda_static_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_honda_static_entry};
const FlipperAppPluginDescriptor* enc_honda_static_ep(void) { return &enc_honda_static_descriptor; }
