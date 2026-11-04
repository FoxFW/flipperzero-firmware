/* enc_honda_v1.fal : SD encoder for honda_v1 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define honda_v1_protocol enc_honda_v1__honda_v1_protocol
#define subghz_protocol_encoder_common_free enc_honda_v1__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_honda_v1__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_honda_v1_alloc enc_honda_v1__subghz_protocol_encoder_honda_v1_alloc
#define subghz_protocol_encoder_honda_v1_deserialize enc_honda_v1__subghz_protocol_encoder_honda_v1_deserialize
#define subghz_protocol_encoder_honda_v1_yield enc_honda_v1__subghz_protocol_encoder_honda_v1_yield
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/honda_v1.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <string.h>
#include <lib/toolbox/level_duration.h>
#include <lib/subghz/protocols/common.h>

#define HONDA_V1_BIT_COUNT             68
#define HONDA_V1_TE_SHORT              1000
#define HONDA_V1_NIBBLE_MASK           0x0FU
#define HONDA_V1_SERIAL_MASK           0x0FFFFFFFU
#define HONDA_V1_COUNTER_MASK          0xFFFFU
#define HONDA_V1_LOW32_MASK            0xFFFFFFFFULL
#define HONDA_V1_BUTTON_MAX            10U
#define HONDA_V1_BUTTON_VALID_MASK     0x701U
#define HONDA_V1_BUTTON_FALLBACK_CODE  0x00088888U
#define HONDA_V1_UPLOAD_CAPACITY       2048U
#define HONDA_V1_PREAMBLE_UPLOAD_COUNT 180U
#define HONDA_V1_FRAME_SYMBOLS         80U
#define HONDA_V1_FRAME_START           12U
#define HONDA_V1_FRAME_SYNC_DROP       2U
#define HONDA_V1_FRAME_REPEAT_PER_CRC  2U
#define HONDA_V1_FRAME_BYTES           9U
#define HONDA_V1_FRAME_CRC_INDEX       8U
#define HONDA_V1_FRAME_GAP_US          5000U
#define HONDA_V1_FRAME_GENERATED_MAX   (HONDA_V1_FRAME_SYMBOLS * 2U)
#define HONDA_V1_FRAME_TAIL_MAX        3U
#define HONDA_V1_KEY_BYTES             8U
#define HONDA_V1_CRC_FIELD             "Crc"
#define HONDA_V1_KEY_2_FIELD           "Key_2"

typedef enum {
    HondaV1ButtonUnlock = 0,
    HondaV1ButtonLock = 8,
    HondaV1ButtonTrunk = 9,
    HondaV1ButtonPanic = 10,
} HondaV1Button;

struct SubGhzProtocolEncoderHondaV1 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t k2;
};

static const uint32_t honda_v1_button_codes[HONDA_V1_BUTTON_MAX + 1U] = {
    [HondaV1ButtonUnlock] = 0x00080808,
    [HondaV1ButtonLock] = 0x00088888,
    [HondaV1ButtonTrunk] = 0x00099190,
    [HondaV1ButtonPanic] = 0x000FA7A0,
};

static size_t honda_v1_emit(
    LevelDuration* upload,
    size_t index,
    size_t capacity,
    bool level,
    uint32_t duration);
static size_t honda_v1_emit_merge(
    LevelDuration* upload,
    size_t index,
    size_t capacity,
    bool level,
    uint32_t duration);
static size_t honda_v1_emit_short_pairs(
    LevelDuration* upload,
    size_t index,
    size_t capacity,
    uint32_t duration,
    size_t count);
static void honda_v1_u64_to_bytes_be(uint64_t data, uint8_t bytes[8]);
static bool honda_v1_button_valid(uint8_t b);
static uint32_t honda_v1_button_code(uint8_t button);
static uint8_t honda_v1_crc_fold(uint16_t v);
static uint8_t honda_v1_checksum_base(uint64_t data);
static uint8_t honda_v1_checksum_alternate(uint8_t checksum);
static void honda_v1_checksum_wire_order(uint64_t data, uint8_t* first, uint8_t* second);
static void honda_v1_decode_fields(SubGhzBlockGeneric* generic);
static uint64_t honda_v1_build_key(uint32_t serial, uint8_t button, uint16_t counter);
static bool honda_v1_append_frame(
    SubGhzProtocolEncoderHondaV1* instance,
    size_t* index,
    const uint8_t frame[HONDA_V1_FRAME_BYTES]);
static bool honda_v1_build_upload(SubGhzProtocolEncoderHondaV1* instance);

static size_t honda_v1_emit(
    LevelDuration* upload,
    size_t index,
    size_t capacity,
    bool level,
    uint32_t duration) {
    if(index < capacity) {
        upload[index++] = level_duration_make(level, duration);
    }
    return index;
}

static size_t honda_v1_emit_merge(
    LevelDuration* upload,
    size_t index,
    size_t capacity,
    bool level,
    uint32_t duration) {
    if(index == 0) {
        if(index < capacity) upload[index++] = level_duration_make(level, duration);
        return index;
    }
    if(index >= capacity) return index;

    LevelDuration prev = upload[index - 1];
    if(level_duration_get_level(prev) == level) {
        upload[index - 1] = level_duration_make(
            level, level_duration_get_duration(prev) + duration);
    } else {
        upload[index++] = level_duration_make(level, duration);
    }
    return index;
}

static size_t honda_v1_emit_short_pairs(
    LevelDuration* upload,
    size_t index,
    size_t capacity,
    uint32_t duration,
    size_t count) {
    for(size_t i = 0; i < count; i++) {
        if((index + 2U) > capacity) return index;
        upload[index++] = level_duration_make(true, duration);
        upload[index++] = level_duration_make(false, duration);
    }
    return index;
}

static void honda_v1_u64_to_bytes_be(uint64_t data, uint8_t bytes[8]) {
    for(size_t index = 0; index < 8; index++) {
        bytes[index] = (data >> ((7U - index) * 8U)) & 0xFFU;
    }
}

static bool honda_v1_button_valid(uint8_t b) {
    if(b > HONDA_V1_BUTTON_MAX) return false;
    return ((HONDA_V1_BUTTON_VALID_MASK >> b) & 1U) != 0U;
}

static uint32_t honda_v1_button_code(uint8_t button) {
    if(!honda_v1_button_valid(button)) {
        return HONDA_V1_BUTTON_FALLBACK_CODE;
    }
    return honda_v1_button_codes[button];
}

static uint8_t honda_v1_crc_fold(uint16_t v) {
    const uint8_t lo = (uint8_t)(v & HONDA_V1_NIBBLE_MASK);
    const uint16_t hi = (uint16_t)(v >> 4U);
    int32_t s = (hi & 1U) ? (int32_t)lo : -(int32_t)lo;
    uint8_t out = (uint8_t)((s - (int32_t)hi) & 7);
    out |= (uint8_t)(((v >> 3U) & 1U) << 3U);
    if(((v >> 1U) & 1U) && (((v >> 4U) ^ (v >> 5U)) & 1U)) {
        out ^= 0x04U;
    }
    return (uint8_t)(out & HONDA_V1_NIBBLE_MASK);
}

static uint8_t honda_v1_checksum_base(uint64_t data) {
    const uint8_t a = honda_v1_crc_fold((uint16_t)(data & HONDA_V1_COUNTER_MASK));
    const uint8_t b = honda_v1_crc_fold((uint8_t)((data >> 40U) & 0xFFU));
    return (uint8_t)((a ^ b ^ 1U) & HONDA_V1_NIBBLE_MASK);
}

static uint8_t honda_v1_checksum_alternate(uint8_t checksum) {
    uint8_t mask = 0x09U;
    if((checksum & 1U) == 0U) {
        mask = (checksum & 2U) ? 0x0BU : HONDA_V1_NIBBLE_MASK;
    }
    return (uint8_t)((checksum ^ mask) & HONDA_V1_NIBBLE_MASK);
}

static void honda_v1_checksum_wire_order(uint64_t data, uint8_t* first, uint8_t* second) {
    const uint8_t checksum = honda_v1_checksum_base(data);
    const uint8_t other = honda_v1_checksum_alternate(checksum);
    if((checksum & 0x08U) != 0U) {
        *first = other;
        *second = checksum;
    } else {
        *first = checksum;
        *second = other;
    }
}

static void honda_v1_decode_fields(SubGhzBlockGeneric* generic) {
    const uint32_t low = (uint32_t)(generic->data & HONDA_V1_LOW32_MASK);
    generic->serial = (uint32_t)((generic->data >> 36U) & HONDA_V1_SERIAL_MASK);
    generic->btn = (uint8_t)((low >> 28U) & HONDA_V1_NIBBLE_MASK);
    generic->cnt = low & HONDA_V1_COUNTER_MASK;
    generic->data_count_bit = HONDA_V1_BIT_COUNT;
}

static uint64_t honda_v1_build_key(uint32_t serial, uint8_t button, uint16_t counter) {
    const uint32_t table = honda_v1_button_code(button);
    const uint32_t low = ((table & HONDA_V1_COUNTER_MASK) << 16U) | counter;
    const uint32_t high = ((serial & HONDA_V1_SERIAL_MASK) << 4U) | (table >> 16U);
    return ((uint64_t)high << 32U) | low;
}

static bool honda_v1_append_frame(
    SubGhzProtocolEncoderHondaV1* instance,
    size_t* index,
    const uint8_t frame[HONDA_V1_FRAME_BYTES]) {
    LevelDuration* upload = instance->encoder.upload;
    LevelDuration generated[HONDA_V1_FRAME_GENERATED_MAX];
    size_t generated_count = 0U;

    for(uint32_t bit_index = 0U; bit_index < HONDA_V1_FRAME_SYMBOLS; bit_index++) {
        uint32_t bit;

        if(bit_index >= HONDA_V1_FRAME_START) {
            const uint32_t data_index = (bit_index - HONDA_V1_FRAME_START) >> 3U;
            const uint8_t shift = (uint8_t)((11U - bit_index) & 0x07U);
            bit = (frame[data_index] >> shift) & 0x01U;
        } else {
            bit = ((uint32_t)~bit_index) & 0x01U;
        }

        generated_count = honda_v1_emit_merge(
            generated, generated_count, COUNT_OF(generated), bit != 0U, HONDA_V1_TE_SHORT);
        generated_count = honda_v1_emit_merge(
            generated, generated_count, COUNT_OF(generated), bit == 0U, HONDA_V1_TE_SHORT);
    }

    if(generated_count <= HONDA_V1_FRAME_SYNC_DROP) {
        return false;
    }

    const size_t copy_count = generated_count - HONDA_V1_FRAME_SYNC_DROP;
    if((*index + copy_count + HONDA_V1_FRAME_TAIL_MAX) > HONDA_V1_UPLOAD_CAPACITY) {
        return false;
    }

    memcpy(
        &upload[*index], &generated[HONDA_V1_FRAME_SYNC_DROP], copy_count * sizeof(LevelDuration));
    *index += copy_count;

    const bool tail_level = !level_duration_get_level(upload[*index - 1U]);
    *index = honda_v1_emit(upload, *index, HONDA_V1_UPLOAD_CAPACITY, tail_level, HONDA_V1_TE_SHORT);
    if(!tail_level) {
        *index = honda_v1_emit(upload, *index, HONDA_V1_UPLOAD_CAPACITY, true, HONDA_V1_TE_SHORT);
    }
    *index = honda_v1_emit(upload, *index, HONDA_V1_UPLOAD_CAPACITY, false, HONDA_V1_FRAME_GAP_US);
    return true;
}

static bool honda_v1_build_upload(SubGhzProtocolEncoderHondaV1* instance) {
    furi_check(instance);

    LevelDuration* upload = instance->encoder.upload;
    if(upload == NULL) return false;

    uint8_t frame[HONDA_V1_FRAME_BYTES] = {0};
    uint8_t first = 0U;
    uint8_t second = 0U;
    size_t index = 0U;

    index = honda_v1_emit_short_pairs(
        upload,
        index,
        HONDA_V1_UPLOAD_CAPACITY,
        HONDA_V1_TE_SHORT,
        HONDA_V1_PREAMBLE_UPLOAD_COUNT / 2U);
    if(index != HONDA_V1_PREAMBLE_UPLOAD_COUNT) {
        return false;
    }
    upload[index - 1U] = level_duration_make(false, HONDA_V1_FRAME_GAP_US);

    honda_v1_u64_to_bytes_be(instance->generic.data, frame);
    honda_v1_checksum_wire_order(instance->generic.data, &first, &second);

    const uint8_t crc_order[] = {first, second};
    for(size_t crc_index = 0U; crc_index < COUNT_OF(crc_order); crc_index++) {
        frame[HONDA_V1_FRAME_CRC_INDEX] = (uint8_t)(crc_order[crc_index] << 4U);
        for(size_t repeat = 0U; repeat < HONDA_V1_FRAME_REPEAT_PER_CRC; repeat++) {
            if(!honda_v1_append_frame(instance, &index, frame)) {
                return false;
            }
        }
    }

    instance->k2 = second;
    instance->encoder.front = 0U;
    instance->encoder.size_upload = index;
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

void* subghz_protocol_encoder_honda_v1_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);

    SubGhzProtocolEncoderHondaV1* instance = malloc(sizeof(SubGhzProtocolEncoderHondaV1));
    furi_check(instance);
    memset(instance, 0, sizeof(*instance));

    instance->base.protocol = &honda_v1_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = 1U;
    instance->encoder.size_upload = HONDA_V1_UPLOAD_CAPACITY;
    instance->encoder.upload = malloc(HONDA_V1_UPLOAD_CAPACITY * sizeof(LevelDuration));
    furi_check(instance->encoder.upload);

    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_honda_v1_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);

    SubGhzProtocolEncoderHondaV1* instance = context;
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

    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, HONDA_V1_BIT_COUNT);
    if(status != SubGhzProtocolStatusOk) {
        return status;
    }

    instance->generic.protocol_name = instance->base.protocol->name;
    honda_v1_decode_fields(&instance->generic);

    uint32_t serial = instance->generic.serial;
    uint32_t btn = instance->generic.btn;
    uint32_t cnt = instance->generic.cnt;
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Serial", &serial, 1);
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Btn", &btn, 1);
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Cnt", &cnt, 1);

    uint32_t honda_v1_txraw = 0;
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "TxRaw", &honda_v1_txraw, 1);
    flipper_format_rewind(flipper_format);

    if(honda_v1_txraw == 0) {
        uint32_t mult = furi_hal_subghz_get_rolling_counter_mult();
        if(mult == 0U) mult = 1U;
        cnt = (cnt + mult) & HONDA_V1_COUNTER_MASK;
    }

    serial &= HONDA_V1_SERIAL_MASK;
    uint8_t button = (uint8_t)(btn & HONDA_V1_NIBBLE_MASK);
    if(!honda_v1_button_valid(button)) {
        button = (uint8_t)instance->generic.btn;
    }
    if(!honda_v1_button_valid(button)) {
        button = HondaV1ButtonUnlock;
    }

    instance->generic.serial = serial;
    instance->generic.btn = button;
    instance->generic.cnt = cnt & HONDA_V1_COUNTER_MASK;
    instance->generic.data_count_bit = HONDA_V1_BIT_COUNT;
    instance->generic.data =
        honda_v1_build_key(instance->generic.serial, instance->generic.btn, instance->generic.cnt);

    uint8_t first = 0U;
    uint8_t second = 0U;
    honda_v1_checksum_wire_order(instance->generic.data, &first, &second);
    instance->k2 = second;

    uint8_t key_data[HONDA_V1_KEY_BYTES];
    honda_v1_u64_to_bytes_be(instance->generic.data, key_data);
    flipper_format_rewind(flipper_format);
    bool key_written =
        flipper_format_update_hex(flipper_format, "Key", key_data, sizeof(key_data));
    if(!key_written) {
        flipper_format_rewind(flipper_format);
        key_written = flipper_format_insert_or_update_hex(
            flipper_format, "Key", key_data, sizeof(key_data));
    }
    if(!key_written) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    uint32_t tmp;
    flipper_format_rewind(flipper_format);
    tmp = instance->generic.serial;
    if(!flipper_format_update_uint32(flipper_format, "Serial", &tmp, 1)) {
        flipper_format_rewind(flipper_format);
        flipper_format_insert_or_update_uint32(flipper_format, "Serial", &tmp, 1);
    }
    flipper_format_rewind(flipper_format);
    tmp = instance->generic.btn;
    if(!flipper_format_update_uint32(flipper_format, "Btn", &tmp, 1)) {
        flipper_format_rewind(flipper_format);
        flipper_format_insert_or_update_uint32(flipper_format, "Btn", &tmp, 1);
    }
    flipper_format_rewind(flipper_format);
    tmp = instance->generic.cnt;
    if(!flipper_format_update_uint32(flipper_format, "Cnt", &tmp, 1)) {
        flipper_format_rewind(flipper_format);
        flipper_format_insert_or_update_uint32(flipper_format, "Cnt", &tmp, 1);
    }
    flipper_format_rewind(flipper_format);
    tmp = instance->k2;
    if(!flipper_format_update_uint32(flipper_format, HONDA_V1_CRC_FIELD, &tmp, 1)) {
        flipper_format_rewind(flipper_format);
        flipper_format_insert_or_update_uint32(flipper_format, HONDA_V1_CRC_FIELD, &tmp, 1);
    }
    flipper_format_rewind(flipper_format);
    tmp = instance->k2;
    if(!flipper_format_update_uint32(flipper_format, HONDA_V1_KEY_2_FIELD, &tmp, 1)) {
        flipper_format_rewind(flipper_format);
        flipper_format_insert_or_update_uint32(flipper_format, HONDA_V1_KEY_2_FIELD, &tmp, 1);
    }

    flipper_format_rewind(flipper_format);
    uint32_t repeat = 1U;
    flipper_format_read_uint32(flipper_format, "Repeat", &repeat, 1);
    instance->encoder.repeat = (size_t)repeat;

    if(!honda_v1_build_upload(instance)) {
        return SubGhzProtocolStatusErrorEncoderGetUpload;
    }

    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

LevelDuration subghz_protocol_encoder_honda_v1_yield(void* context) {
    SubGhzProtocolEncoderHondaV1* instance = context;

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

static const SubGhzProtocolEncoder enc_honda_v1_vt = {
    .alloc = subghz_protocol_encoder_honda_v1_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_honda_v1_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_honda_v1_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol honda_v1_protocol = {
    .name = HONDA_V1_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = NULL,
    .encoder = &enc_honda_v1_vt,
};

static const SubGhzEncodePluginEntry enc_honda_v1_entry = {.name = HONDA_V1_PROTOCOL_NAME, .encoder = &enc_honda_v1_vt};
static const FlipperAppPluginDescriptor enc_honda_v1_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_honda_v1_entry};
const FlipperAppPluginDescriptor* enc_honda_v1_ep(void) { return &enc_honda_v1_descriptor; }
