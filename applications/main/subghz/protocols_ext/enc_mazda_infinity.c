/* enc_mazda_infinity.fal : SD encoder for mazda_infinity (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_free enc_mazda_infinity__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_yield enc_mazda_infinity__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_mazda_infinity_alloc enc_mazda_infinity__subghz_protocol_encoder_mazda_infinity_alloc
#define subghz_protocol_encoder_mazda_infinity_deserialize enc_mazda_infinity__subghz_protocol_encoder_mazda_infinity_deserialize
#define subghz_protocol_encoder_mazda_infinity_stop enc_mazda_infinity__subghz_protocol_encoder_mazda_infinity_stop
#define subghz_protocol_mazda_infinity enc_mazda_infinity__subghz_protocol_mazda_infinity
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/mazda_infinity.h>
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

#define MAZDA_INF_TE_SHORT   250U
#define MAZDA_INF_GAP_US     5000U
#define MAZDA_INF_PREAMBLE_BYTES 12U
#define MAZDA_INF_PAYLOAD_BYTES  8U
#define MAZDA_INF_PAYLOAD_BITS   64U
#define MAZDA_INF_TRAILER        0x5AU
#define MAZDA_INF_SYNC0 0xFFU
#define MAZDA_INF_SYNC1 0xFFU
#define MAZDA_INF_SYNC2 0xD7U
#define MAZDA_INF_BTN_LOCK   0x10U
#define MAZDA_INF_BTN_UNLOCK 0x20U
#define MAZDA_INF_BTN_TRUNK  0x40U
#define MAZDA_INF_DEFAULT_REPEAT 4U
#define MAZDA_INF_WIRE_BYTES (MAZDA_INF_PREAMBLE_BYTES + 3U + MAZDA_INF_PAYLOAD_BYTES + 1U)
#define MAZDA_INF_UPLOAD_CAPACITY \
    ((MAZDA_INF_WIRE_BYTES * 8U * 2U) + 8U)
#define MAZDA_INF_DATA_FIELD "Data"

struct SubGhzProtocolEncoderMazdaInfinity {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t logical[MAZDA_INF_PAYLOAD_BYTES];
};

// ---------------------------------------------------------------------------
// Whitening / interleave primitives (verified against 0x12a90 & 0x10994).
// All are self-inverse, so the same routine is used to build and to reverse.
// ---------------------------------------------------------------------------

static uint8_t mazda_inf_parity(uint8_t v);
static uint8_t mazda_inf_checksum(const uint8_t b[MAZDA_INF_PAYLOAD_BYTES]);
// Step 2: counter interleave (self-inverse merge of b5/b6).
static void mazda_inf_interleave(uint8_t b[MAZDA_INF_PAYLOAD_BYTES]);
// Step 3: data whitening keyed by popcount(b7)&1 (self-inverse XOR).
static void mazda_inf_whiten(uint8_t b[MAZDA_INF_PAYLOAD_BYTES]);
// Build the 8 whitened+inverted on-wire data bytes from the logical payload.
//   logical -> checksum -> interleave -> whiten -> one's-complement
static void mazda_inf_logical_to_wire(
    const uint8_t logical[MAZDA_INF_PAYLOAD_BYTES],
    uint8_t wire[MAZDA_INF_PAYLOAD_BYTES]);
static uint32_t mazda_inf_uid(const uint8_t logical[MAZDA_INF_PAYLOAD_BYTES]);
static uint16_t mazda_inf_counter(const uint8_t logical[MAZDA_INF_PAYLOAD_BYTES]);
static void mazda_inf_decode_fields(SubGhzBlockGeneric* generic, const uint8_t logical[MAZDA_INF_PAYLOAD_BYTES]);
static bool mazda_inf_frame_valid(const uint8_t logical[MAZDA_INF_PAYLOAD_BYTES]);
// ---------------------------------------------------------------------------
// Encoder: exact inverse of the decode path.
//   logical -> (checksum, interleave, whiten, invert) -> 8 wire data bytes,
//   then emit: 12x Manchester(0xFF) + FF FF D7 + 8 wire bytes + 0x5A trailer,
//   Manchester Te=250, repeat 4.
//     bit1 -> HIGH 250 , LOW 250
//     bit0 -> LOW 250 , HIGH 250
// Replay-capable: the stored logical bytes reproduce the exact wire.
// ---------------------------------------------------------------------------

static size_t mazda_inf_emit_byte(LevelDuration* upload, size_t index, uint8_t byte);
static bool mazda_inf_encoder_build_upload(SubGhzProtocolEncoderMazdaInfinity* instance);

// ---------------------------------------------------------------------------
// Whitening / interleave primitives (verified against 0x12a90 & 0x10994).
// All are self-inverse, so the same routine is used to build and to reverse.
// ---------------------------------------------------------------------------

static uint8_t mazda_inf_parity(uint8_t v) {
    v ^= (uint8_t)(v >> 4);
    v ^= (uint8_t)(v >> 2);
    v ^= (uint8_t)(v >> 1);
    return (uint8_t)(v & 1U);
}

static uint8_t mazda_inf_checksum(const uint8_t b[MAZDA_INF_PAYLOAD_BYTES]) {
    uint32_t sum = 0U;
    for(size_t i = 0U; i < 7U; i++) {
        sum += b[i];
    }
    return (uint8_t)(sum & 0xFFU);
}

// Step 2: counter interleave (self-inverse merge of b5/b6).
static void mazda_inf_interleave(uint8_t b[MAZDA_INF_PAYLOAD_BYTES]) {
    const uint8_t b5 = b[5];
    const uint8_t b6 = b[6];
    b[5] = (uint8_t)((b5 & 0xAAU) | (b6 & 0x55U));
    b[6] = (uint8_t)((b5 & 0x55U) | (b6 & 0xAAU));
}

// Step 3: data whitening keyed by popcount(b7)&1 (self-inverse XOR).
static void mazda_inf_whiten(uint8_t b[MAZDA_INF_PAYLOAD_BYTES]) {
    const uint8_t p = mazda_inf_parity(b[7]);
    if(p == 1U) {
        for(size_t k = 0U; k < 6U; k++) {
            b[k] ^= b[6];
        }
    } else {
        b[0] ^= b[5];
        b[1] ^= b[5];
        b[2] ^= b[5];
        b[3] ^= b[5];
        b[4] ^= b[5];
        b[6] ^= b[5];
    }
}

// Build the 8 whitened+inverted on-wire data bytes from the logical payload.
//   logical -> checksum -> interleave -> whiten -> one's-complement
static void mazda_inf_logical_to_wire(
    const uint8_t logical[MAZDA_INF_PAYLOAD_BYTES],
    uint8_t wire[MAZDA_INF_PAYLOAD_BYTES]) {
    uint8_t b[MAZDA_INF_PAYLOAD_BYTES];
    memcpy(b, logical, MAZDA_INF_PAYLOAD_BYTES);
    b[7] = mazda_inf_checksum(b); // step 1
    mazda_inf_interleave(b); // step 2
    mazda_inf_whiten(b); // step 3
    for(size_t i = 0U; i < MAZDA_INF_PAYLOAD_BYTES; i++) {
        wire[i] = (uint8_t)(255U - b[i]); // step 4: one's complement
    }
}

static uint32_t mazda_inf_uid(const uint8_t logical[MAZDA_INF_PAYLOAD_BYTES]) {
    return ((uint32_t)logical[0] << 24U) | ((uint32_t)logical[1] << 16U) |
           ((uint32_t)logical[2] << 8U) | logical[3];
}

static uint16_t mazda_inf_counter(const uint8_t logical[MAZDA_INF_PAYLOAD_BYTES]) {
    return (uint16_t)(((uint16_t)logical[5] << 8U) | logical[6]);
}

static void mazda_inf_decode_fields(SubGhzBlockGeneric* generic, const uint8_t logical[MAZDA_INF_PAYLOAD_BYTES]) {
    generic->serial = mazda_inf_uid(logical);
    generic->btn = logical[4];
    generic->cnt = mazda_inf_counter(logical);
    generic->data_count_bit = MAZDA_INF_PAYLOAD_BITS;
    // 64-bit logical payload fits in generic.data (MSB-first over b0..b7).
    uint64_t data = 0U;
    for(size_t i = 0U; i < MAZDA_INF_PAYLOAD_BYTES; i++) {
        data = (data << 8U) | logical[i];
    }
    generic->data = data;
}

static bool mazda_inf_frame_valid(const uint8_t logical[MAZDA_INF_PAYLOAD_BYTES]) {
    const uint32_t uid = mazda_inf_uid(logical);
    return uid != 0U && uid != UINT32_MAX;
}

// ---------------------------------------------------------------------------
// Encoder: exact inverse of the decode path.
//   logical -> (checksum, interleave, whiten, invert) -> 8 wire data bytes,
//   then emit: 12x Manchester(0xFF) + FF FF D7 + 8 wire bytes + 0x5A trailer,
//   Manchester Te=250, repeat 4.
//     bit1 -> HIGH 250 , LOW 250
//     bit0 -> LOW 250 , HIGH 250
// Replay-capable: the stored logical bytes reproduce the exact wire.
// ---------------------------------------------------------------------------

static size_t mazda_inf_emit_byte(LevelDuration* upload, size_t index, uint8_t byte) {
    const uint32_t te = MAZDA_INF_TE_SHORT;
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

static bool mazda_inf_encoder_build_upload(SubGhzProtocolEncoderMazdaInfinity* instance) {
    furi_check(instance);
    LevelDuration* upload = instance->encoder.upload;
    if(!upload) return false;

    uint8_t wire[MAZDA_INF_PAYLOAD_BYTES];
    mazda_inf_logical_to_wire(instance->logical, wire);

    size_t index = 0U;

    // 12x Manchester of 0xFF (all-ones preamble).
    for(uint8_t i = 0U; i < MAZDA_INF_PREAMBLE_BYTES; i++) {
        index = mazda_inf_emit_byte(upload, index, 0xFFU);
    }

    // Bit-sync gap (LOW) between preamble and sync.
    upload[index++] = level_duration_make(false, MAZDA_INF_GAP_US);

    // Sync FF FF D7 (RAW).
    index = mazda_inf_emit_byte(upload, index, MAZDA_INF_SYNC0);
    index = mazda_inf_emit_byte(upload, index, MAZDA_INF_SYNC1);
    index = mazda_inf_emit_byte(upload, index, MAZDA_INF_SYNC2);

    // 8 payload bytes (already inverted/whitened wire bytes).
    for(uint8_t i = 0U; i < MAZDA_INF_PAYLOAD_BYTES; i++) {
        index = mazda_inf_emit_byte(upload, index, wire[i]);
    }

    // Trailer 0x5A (RAW).
    index = mazda_inf_emit_byte(upload, index, MAZDA_INF_TRAILER);

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

void* subghz_protocol_encoder_mazda_infinity_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderMazdaInfinity* instance =
        calloc(1, sizeof(SubGhzProtocolEncoderMazdaInfinity));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_mazda_infinity;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = MAZDA_INF_DEFAULT_REPEAT;
    instance->encoder.size_upload = 0U;
    instance->encoder.upload = NULL;
    instance->encoder.is_running = false;
    instance->encoder.front = 0U;
    return instance;
}

void subghz_protocol_encoder_mazda_infinity_stop(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderMazdaInfinity* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0U;
}

SubGhzProtocolStatus subghz_protocol_encoder_mazda_infinity_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_check(context);
    furi_check(flipper_format);
    SubGhzProtocolEncoderMazdaInfinity* instance = context;

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
       bit_count != MAZDA_INF_PAYLOAD_BITS) {
        return SubGhzProtocolStatusErrorValueBitCount;
    }
    instance->generic.data_count_bit = (uint16_t)bit_count;

    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(
           flipper_format, MAZDA_INF_DATA_FIELD, instance->logical, MAZDA_INF_PAYLOAD_BYTES) ||
       !mazda_inf_frame_valid(instance->logical)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    instance->generic.serial = mazda_inf_uid(instance->logical);
    instance->generic.btn = instance->logical[4];
    instance->generic.cnt = mazda_inf_counter(instance->logical);

    // [PROTOPIRATE_PORT] custom_btn support.
    // Mazda Infinity buttons: Lock=0x10, Unlock=0x20, Trunk=0x40. OK replays the
    // captured b4. The D-pad selects a known button; on a change the 16-bit
    // rolling counter is advanced so the re-encoded frame is a valid new press.
    // b5/b6 hold the logical counter (high/low); interleave/whiten/checksum are
    // recomputed inside mazda_inf_logical_to_wire, so any change round-trips.
    const uint8_t original_btn = instance->logical[4];
    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(original_btn);
    }
    subghz_custom_btn_set_max(3);
    const uint8_t custom_btn_id = subghz_custom_btn_get();
    uint8_t new_btn = original_btn;
    switch(custom_btn_id) {
    case SUBGHZ_CUSTOM_BTN_UP:    new_btn = MAZDA_INF_BTN_LOCK; break;
    case SUBGHZ_CUSTOM_BTN_DOWN:  new_btn = MAZDA_INF_BTN_UNLOCK; break;
    case SUBGHZ_CUSTOM_BTN_LEFT:  new_btn = MAZDA_INF_BTN_TRUNK; break;
    case SUBGHZ_CUSTOM_BTN_OK:
    default:                      new_btn = original_btn; break; // exact replay
    }

    if(new_btn != original_btn) {
        instance->logical[4] = new_btn;
        // Advance the rolling counter for a genuine (non-replay) press, honoring
        // an explicit framework override if one is set.
        uint32_t new_counter = instance->generic.cnt;
        uint32_t override_cnt = 0U;
        if(subghz_block_generic_global_counter_override_get(&override_cnt)) {
            new_counter = override_cnt;
        } else {
            new_counter += (uint32_t)furi_hal_subghz_get_rolling_counter_mult();
        }
        new_counter &= 0xFFFFU;
        instance->logical[5] = (uint8_t)((new_counter >> 8U) & 0xFFU);
        instance->logical[6] = (uint8_t)(new_counter & 0xFFU);
        instance->logical[7] = mazda_inf_checksum(instance->logical);
        instance->generic.btn = new_btn;
        instance->generic.cnt = (uint16_t)new_counter;
    }

    mazda_inf_decode_fields(&instance->generic, instance->logical);

    uint32_t repeat = MAZDA_INF_DEFAULT_REPEAT;
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Repeat", &repeat, 1);
    instance->encoder.repeat = (repeat == 0U) ? MAZDA_INF_DEFAULT_REPEAT : (size_t)repeat;

    if(instance->encoder.upload == NULL) {
        instance->encoder.upload = malloc(MAZDA_INF_UPLOAD_CAPACITY * sizeof(LevelDuration));
        furi_check(instance->encoder.upload);
    }
    if(!mazda_inf_encoder_build_upload(instance)) {
        return SubGhzProtocolStatusErrorEncoderGetUpload;
    }

    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

static const SubGhzProtocolEncoder enc_mazda_infinity_vt = {
    .alloc = subghz_protocol_encoder_mazda_infinity_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_mazda_infinity_deserialize,
    .stop = subghz_protocol_encoder_mazda_infinity_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_mazda_infinity = {
    .name = MAZDA_INFINITY_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_mazda_infinity_vt,
};

static const SubGhzEncodePluginEntry enc_mazda_infinity_entry = {.name = MAZDA_INFINITY_PROTOCOL_NAME, .encoder = &enc_mazda_infinity_vt};
static const FlipperAppPluginDescriptor enc_mazda_infinity_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_mazda_infinity_entry};
const FlipperAppPluginDescriptor* enc_mazda_infinity_ep(void) { return &enc_mazda_infinity_descriptor; }
