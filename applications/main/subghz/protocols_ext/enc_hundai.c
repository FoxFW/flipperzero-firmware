/* enc_hundai.fal : SD encoder for hundai (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_free enc_hundai__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_yield enc_hundai__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_hundai_alloc enc_hundai__subghz_protocol_encoder_hundai_alloc
#define subghz_protocol_encoder_hundai_deserialize enc_hundai__subghz_protocol_encoder_hundai_deserialize
#define subghz_protocol_encoder_hundai_stop enc_hundai__subghz_protocol_encoder_hundai_stop
#define subghz_protocol_hundai enc_hundai__subghz_protocol_hundai
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/hundai.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <furi.h>
#include <furi_hal.h>
#include <string.h>
#include <lib/subghz/protocols/common.h>

#define HUNDAI_TE_SHORT       500U
#define HUNDAI_HDR_LOW        4500U
#define HUNDAI_HDR_HIGH       1000U
#define HUNDAI_HDR_LOW2       500U
#define HUNDAI_IFG_LOW        2500U
#define HUNDAI_PREAMBLE_BYTES 32U
#define HUNDAI_PREAMBLE_BYTE  0x55U
#define HUNDAI_WIRE_BYTES     9U
#define HUNDAI_WIRE_BITS      72U
#define HUNDAI_BTN_0 0x01U
#define HUNDAI_BTN_1 0x02U
#define HUNDAI_BTN_2 0x09U
#define HUNDAI_DEFAULT_REPEAT 2U
#define HUNDAI_FRAME_CELLS \
    (3U + (HUNDAI_PREAMBLE_BYTES * 8U * 2U) + (HUNDAI_WIRE_BITS * 2U))
#define HUNDAI_UPLOAD_CAPACITY ((HUNDAI_FRAME_CELLS * 2U) + 8U)
#define HUNDAI_DATA_FIELD "Data"

struct SubGhzProtocolEncoderHundai {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t raw_data[HUNDAI_WIRE_BYTES];
};

// ---------------------------------------------------------------------------
// Field helpers
// ---------------------------------------------------------------------------

static uint32_t hundai_uid(const uint8_t raw[HUNDAI_WIRE_BYTES]);
static uint8_t hundai_button(const uint8_t raw[HUNDAI_WIRE_BYTES]);
static bool hundai_frame_valid(const uint8_t raw[HUNDAI_WIRE_BYTES]);
static void hundai_decode_fields(SubGhzBlockGeneric* generic, const uint8_t raw[HUNDAI_WIRE_BYTES]);
// b7 high-nibble key-index rollover: advance when the session counter b6 wraps
// to 0. new_hi = 1 + old_hi; if that would be 0 (old_hi == 15), reset to 1.
// Applied together with the b6 increment for a genuine (non-replay) press.
static void hundai_advance_counter(uint8_t raw[HUNDAI_WIRE_BYTES]);
// ---------------------------------------------------------------------------
// Encoder: rebuild the exact TX wire and send the frame TWICE.
//   Per frame:
//     header  : LOW 4500 -> HIGH 1000 -> LOW 500
//     preamble: 32x 0x55 Manchester (MSB-first)
//     body    : 9 bytes Manchester (MSB-first)
//       bit1 -> HIGH 500 , LOW 500
//       bit0 -> LOW 500 , HIGH 500
//   inter-frame gap between the two frames: LOW 2500 -> HIGH 1000 -> LOW 500
// Replay-based: the stored 9 bytes reproduce the wire exactly. A D-pad button
// change re-stamps b5's button nibble and advances b6 (+ b7 key-index on wrap).
// ---------------------------------------------------------------------------

static size_t hundai_emit_byte_msb(LevelDuration* upload, size_t index, uint8_t byte);
static size_t hundai_emit_frame(LevelDuration* upload, size_t index, const uint8_t raw[HUNDAI_WIRE_BYTES], bool first_frame);
static bool hundai_encoder_build_upload(SubGhzProtocolEncoderHundai* instance);

// ---------------------------------------------------------------------------
// Field helpers
// ---------------------------------------------------------------------------

static uint32_t hundai_uid(const uint8_t raw[HUNDAI_WIRE_BYTES]) {
    // b0..b3 as the 32-bit UID view (b4/b8 are extra serial/check bytes).
    return ((uint32_t)raw[0] << 24U) | ((uint32_t)raw[1] << 16U) | ((uint32_t)raw[2] << 8U) |
           raw[3];
}

static uint8_t hundai_button(const uint8_t raw[HUNDAI_WIRE_BYTES]) {
    return (uint8_t)(raw[5] & 0x0FU);
}

static bool hundai_frame_valid(const uint8_t raw[HUNDAI_WIRE_BYTES]) {
    const uint32_t uid = hundai_uid(raw);
    return uid != 0U && uid != UINT32_MAX;
}

static void hundai_decode_fields(SubGhzBlockGeneric* generic, const uint8_t raw[HUNDAI_WIRE_BYTES]) {
    generic->serial = hundai_uid(raw);
    generic->btn = hundai_button(raw);
    generic->cnt = raw[6];
    generic->data_count_bit = HUNDAI_WIRE_BITS;
    // 72 bits do not fit generic.data; keep a 64-bit digest (b1..b8) for hashing.
    uint64_t data = 0U;
    for(size_t i = 1U; i < HUNDAI_WIRE_BYTES; i++) data = (data << 8U) | raw[i];
    generic->data = data;
}

// b7 high-nibble key-index rollover: advance when the session counter b6 wraps
// to 0. new_hi = 1 + old_hi; if that would be 0 (old_hi == 15), reset to 1.
// Applied together with the b6 increment for a genuine (non-replay) press.
static void hundai_advance_counter(uint8_t raw[HUNDAI_WIRE_BYTES]) {
    const uint8_t new_b6 = (uint8_t)(raw[6] + 1U);
    raw[6] = new_b6;
    if(new_b6 == 0U) {
        uint8_t hi = (uint8_t)((raw[7] >> 4U) & 0x0FU);
        uint8_t new_hi = (uint8_t)((hi + 1U) & 0x0FU);
        if(new_hi == 0U) new_hi = 1U; // never 0; wrap 15 -> 1
        raw[7] = (uint8_t)((new_hi << 4U) | (raw[7] & 0x0FU)); // preserve channel nibble
    }
}

// ---------------------------------------------------------------------------
// Encoder: rebuild the exact TX wire and send the frame TWICE.
//   Per frame:
//     header  : LOW 4500 -> HIGH 1000 -> LOW 500
//     preamble: 32x 0x55 Manchester (MSB-first)
//     body    : 9 bytes Manchester (MSB-first)
//       bit1 -> HIGH 500 , LOW 500
//       bit0 -> LOW 500 , HIGH 500
//   inter-frame gap between the two frames: LOW 2500 -> HIGH 1000 -> LOW 500
// Replay-based: the stored 9 bytes reproduce the wire exactly. A D-pad button
// change re-stamps b5's button nibble and advances b6 (+ b7 key-index on wrap).
// ---------------------------------------------------------------------------

static size_t hundai_emit_byte_msb(LevelDuration* upload, size_t index, uint8_t byte) {
    const uint32_t te = HUNDAI_TE_SHORT;
    for(uint8_t bit = 0U; bit < 8U; bit++) {
        const bool value = ((byte >> (7U - bit)) & 1U) != 0U;
        if(value) {
            // bit1 = HIGH then LOW
            upload[index++] = level_duration_make(true, te);
            upload[index++] = level_duration_make(false, te);
        } else {
            // bit0 = LOW then HIGH
            upload[index++] = level_duration_make(false, te);
            upload[index++] = level_duration_make(true, te);
        }
    }
    return index;
}

static size_t hundai_emit_frame(LevelDuration* upload, size_t index, const uint8_t raw[HUNDAI_WIRE_BYTES], bool first_frame) {
    // Header: LOW (4500 frame / 2500 inter-frame) -> HIGH 1000 -> LOW 500.
    upload[index++] =
        level_duration_make(false, first_frame ? HUNDAI_HDR_LOW : HUNDAI_IFG_LOW);
    upload[index++] = level_duration_make(true, HUNDAI_HDR_HIGH);
    upload[index++] = level_duration_make(false, HUNDAI_HDR_LOW2);

    // 32 x 0x55 preamble (MSB-first Manchester).
    for(uint8_t i = 0U; i < HUNDAI_PREAMBLE_BYTES; i++) {
        index = hundai_emit_byte_msb(upload, index, HUNDAI_PREAMBLE_BYTE);
    }

    // 9 body bytes (MSB-first Manchester).
    for(uint8_t i = 0U; i < HUNDAI_WIRE_BYTES; i++) {
        index = hundai_emit_byte_msb(upload, index, raw[i]);
    }
    return index;
}

static bool hundai_encoder_build_upload(SubGhzProtocolEncoderHundai* instance) {
    furi_check(instance);
    LevelDuration* upload = instance->encoder.upload;
    if(!upload) return false;

    size_t index = 0U;
    // Frame sent TWICE per press. The first frame opens with the 4500us header;
    // the second is preceded by the inter-frame gap (2500us) header sequence.
    index = hundai_emit_frame(upload, index, instance->raw_data, true);
    index = hundai_emit_frame(upload, index, instance->raw_data, false);

    // Trailing gap so consecutive repeats are separable.
    upload[index++] = level_duration_make(false, HUNDAI_IFG_LOW);

    instance->encoder.size_upload = index;
    instance->encoder.front = 0U;
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

void* subghz_protocol_encoder_hundai_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderHundai* instance = calloc(1, sizeof(SubGhzProtocolEncoderHundai));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_hundai;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = HUNDAI_DEFAULT_REPEAT;
    instance->encoder.size_upload = 0U;
    instance->encoder.upload = NULL;
    instance->encoder.is_running = false;
    instance->encoder.front = 0U;
    return instance;
}

void subghz_protocol_encoder_hundai_stop(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderHundai* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0U;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_hundai_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    furi_check(flipper_format);
    SubGhzProtocolEncoderHundai* instance = context;

    instance->encoder.is_running = false;
    instance->encoder.front = 0U;

    flipper_format_rewind(flipper_format);
    FuriString* protocol_name = furi_string_alloc();
    bool protocol_ok = flipper_format_read_string(flipper_format, "Protocol", protocol_name) &&
                       furi_string_equal(protocol_name, instance->base.protocol->name);
    furi_string_free(protocol_name);
    if(!protocol_ok) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    uint32_t bit_count = 0U;
    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_uint32(flipper_format, "Bit", &bit_count, 1) ||
       bit_count != HUNDAI_WIRE_BITS) {
        return SubGhzProtocolStatusErrorValueBitCount;
    }
    instance->generic.data_count_bit = (uint16_t)bit_count;

    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(
           flipper_format, HUNDAI_DATA_FIELD, instance->raw_data, HUNDAI_WIRE_BYTES) ||
       !hundai_frame_valid(instance->raw_data)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    instance->generic.serial = hundai_uid(instance->raw_data);
    instance->generic.btn = hundai_button(instance->raw_data);
    instance->generic.cnt = instance->raw_data[6];

    // [PROTOPIRATE_PORT] custom_btn support. Hundai exposes 3 buttons via the b5
    // low nibble: Lock=0x01, Unlock=0x02, Trunk=0x09. OK replays the captured
    // frame byte-identically. A D-pad change re-stamps b5's button nibble (high
    // nibble preserved) and advances the session counter b6 (with b7 key-index
    // rollover on wrap) so the emitted frame is a valid fresh press.
    const uint8_t original_btn = hundai_button(instance->raw_data);
    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(original_btn);
    }
    subghz_custom_btn_set_max(3);
    const uint8_t custom_btn_id = subghz_custom_btn_get();
    uint8_t new_btn = original_btn;
    switch(custom_btn_id) {
    case SUBGHZ_CUSTOM_BTN_UP:   new_btn = HUNDAI_BTN_0; break;
    case SUBGHZ_CUSTOM_BTN_DOWN: new_btn = HUNDAI_BTN_1; break;
    case SUBGHZ_CUSTOM_BTN_LEFT: new_btn = HUNDAI_BTN_2; break;
    case SUBGHZ_CUSTOM_BTN_OK:
    default:                     new_btn = original_btn; break; // exact replay
    }

    if(new_btn != original_btn) {
        // Re-stamp the button nibble (preserve b5 high nibble).
        instance->raw_data[5] = (uint8_t)((instance->raw_data[5] & 0xF0U) | (new_btn & 0x0FU));
        // Advance the session counter (honoring an explicit override if set).
        uint32_t override_cnt = 0U;
        if(subghz_block_generic_global_counter_override_get(&override_cnt)) {
            instance->raw_data[6] = (uint8_t)(override_cnt & 0xFFU);
        } else {
            hundai_advance_counter(instance->raw_data);
        }
        instance->generic.btn = hundai_button(instance->raw_data);
        instance->generic.cnt = instance->raw_data[6];
    }

    hundai_decode_fields(&instance->generic, instance->raw_data);

    uint32_t repeat = HUNDAI_DEFAULT_REPEAT;
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Repeat", &repeat, 1);
    instance->encoder.repeat = (repeat == 0U) ? HUNDAI_DEFAULT_REPEAT : (size_t)repeat;

    if(instance->encoder.upload == NULL) {
        instance->encoder.upload = malloc(HUNDAI_UPLOAD_CAPACITY * sizeof(LevelDuration));
        furi_check(instance->encoder.upload);
    }
    if(!hundai_encoder_build_upload(instance)) {
        return SubGhzProtocolStatusErrorEncoderGetUpload;
    }

    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

static const SubGhzProtocolEncoder enc_hundai_vt = {
    .alloc = subghz_protocol_encoder_hundai_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_hundai_deserialize,
    .stop = subghz_protocol_encoder_hundai_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_hundai = {
    .name = HUNDAI_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_hundai_vt,
};

static const SubGhzEncodePluginEntry enc_hundai_entry = {.name = HUNDAI_PROTOCOL_NAME, .encoder = &enc_hundai_vt};
static const FlipperAppPluginDescriptor enc_hundai_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_hundai_entry};
const FlipperAppPluginDescriptor* enc_hundai_ep(void) { return &enc_hundai_descriptor; }
