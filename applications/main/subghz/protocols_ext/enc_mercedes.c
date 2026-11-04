/* enc_mercedes.fal : SD encoder for mercedes (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_free enc_mercedes__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_yield enc_mercedes__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_mercedes_alloc enc_mercedes__subghz_protocol_encoder_mercedes_alloc
#define subghz_protocol_encoder_mercedes_deserialize enc_mercedes__subghz_protocol_encoder_mercedes_deserialize
#define subghz_protocol_encoder_mercedes_stop enc_mercedes__subghz_protocol_encoder_mercedes_stop
#define subghz_protocol_mercedes enc_mercedes__subghz_protocol_mercedes
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/mercedes.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <furi.h>
#include <string.h>
#include <lib/subghz/protocols/common.h>

#define MERCEDES_TE_SHORT      500U
#define MERCEDES_LEAD_GAP_US   3700U
#define MERCEDES_PREAMBLE_PAIRS 55U
#define MERCEDES_WIRE_BYTES    14U
#define MERCEDES_WIRE_BITS     112U
#define MERCEDES_UPLOAD_CAPACITY \
    (1U + (MERCEDES_PREAMBLE_PAIRS * 2U) + 4U + (MERCEDES_WIRE_BITS * 2U) + 2U)
#define MERCEDES_DEFAULT_REPEAT 4U
#define MERCEDES_DATA_FIELD "Data"

struct SubGhzProtocolEncoderMercedes {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t raw_data[MERCEDES_WIRE_BYTES];
};

// ---------------------------------------------------------------------------
// Shared field helpers
// ---------------------------------------------------------------------------

static uint8_t mercedes_checksum(const uint8_t raw[MERCEDES_WIRE_BYTES]);
static uint32_t mercedes_uid(const uint8_t raw[MERCEDES_WIRE_BYTES]);
static uint16_t mercedes_counter(const uint8_t raw[MERCEDES_WIRE_BYTES]);
static bool mercedes_frame_valid(const uint8_t raw[MERCEDES_WIRE_BYTES]);
// ---------------------------------------------------------------------------
// Encoder: rebuild the exact TX wire.
//   3700 us lead gap
//   + 55x { HIGH 500 / LOW 500 } preamble
//   + sync boundary (LOW 500 then HIGH 500)
//   + 112 Manchester bits of the 14 raw bytes (MSB-first)
//       bit1 -> LOW 500 , HIGH 500
//       bit0 -> HIGH 500 , LOW 500
//   + trailing gap
// Replay-based (rolling code / no key). D-pad only re-stamps b4.
// ---------------------------------------------------------------------------

static bool mercedes_encoder_build_upload(SubGhzProtocolEncoderMercedes* instance);

// ---------------------------------------------------------------------------
// Shared field helpers
// ---------------------------------------------------------------------------

static uint8_t mercedes_checksum(const uint8_t raw[MERCEDES_WIRE_BYTES]) {
    uint32_t sum = 0U;
    for(size_t i = 0U; i < 7U; i++) {
        sum += raw[i];
    }
    return (uint8_t)(sum & 0xFFU);
}

static uint32_t mercedes_uid(const uint8_t raw[MERCEDES_WIRE_BYTES]) {
    return ((uint32_t)raw[0] << 24U) | ((uint32_t)raw[1] << 16U) | ((uint32_t)raw[2] << 8U) |
           raw[3];
}

static uint16_t mercedes_counter(const uint8_t raw[MERCEDES_WIRE_BYTES]) {
    return (uint16_t)(((uint16_t)raw[5] << 8U) | raw[6]);
}

static bool mercedes_frame_valid(const uint8_t raw[MERCEDES_WIRE_BYTES]) {
    // The checksum is display-only (may be opaque on real captures), so validity
    // is intentionally loose: reject only the empty / all-ones degenerate frames.
    const uint32_t uid = mercedes_uid(raw);
    return uid != 0U && uid != UINT32_MAX;
}

// ---------------------------------------------------------------------------
// Encoder: rebuild the exact TX wire.
//   3700 us lead gap
//   + 55x { HIGH 500 / LOW 500 } preamble
//   + sync boundary (LOW 500 then HIGH 500)
//   + 112 Manchester bits of the 14 raw bytes (MSB-first)
//       bit1 -> LOW 500 , HIGH 500
//       bit0 -> HIGH 500 , LOW 500
//   + trailing gap
// Replay-based (rolling code / no key). D-pad only re-stamps b4.
// ---------------------------------------------------------------------------

static bool mercedes_encoder_build_upload(SubGhzProtocolEncoderMercedes* instance) {
    furi_check(instance);
    LevelDuration* upload = instance->encoder.upload;
    if(!upload) return false;

    size_t index = 0U;
    const uint32_t te = MERCEDES_TE_SHORT;

    // Lead gap (LOW).
    upload[index++] = level_duration_make(false, MERCEDES_LEAD_GAP_US);

    // 55 preamble cycles { HIGH 500 / LOW 500 }.
    for(uint32_t i = 0U; i < MERCEDES_PREAMBLE_PAIRS; i++) {
        upload[index++] = level_duration_make(true, te);
        upload[index++] = level_duration_make(false, te);
    }

    // Sync boundary: extra LOW 500 then HIGH 500.
    upload[index++] = level_duration_make(false, te);
    upload[index++] = level_duration_make(true, te);

    // 112 Manchester data bits, MSB-first.
    for(uint8_t bit_index = 0U; bit_index < MERCEDES_WIRE_BITS; bit_index++) {
        const bool bit =
            ((instance->raw_data[bit_index >> 3U] >> (7U - (bit_index & 7U))) & 1U) != 0U;
        if(bit) {
            // bit1 = LOW then HIGH
            upload[index++] = level_duration_make(false, te);
            upload[index++] = level_duration_make(true, te);
        } else {
            // bit0 = HIGH then LOW
            upload[index++] = level_duration_make(true, te);
            upload[index++] = level_duration_make(false, te);
        }
    }

    // Trailing gap so consecutive repeats are separable.
    upload[index++] = level_duration_make(false, MERCEDES_LEAD_GAP_US);

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

void* subghz_protocol_encoder_mercedes_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderMercedes* instance = calloc(1, sizeof(SubGhzProtocolEncoderMercedes));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_mercedes;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = MERCEDES_DEFAULT_REPEAT;
    instance->encoder.size_upload = 0U;
    instance->encoder.upload = NULL;
    instance->encoder.is_running = false;
    instance->encoder.front = 0U;
    return instance;
}

void subghz_protocol_encoder_mercedes_stop(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderMercedes* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0U;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_mercedes_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    furi_check(flipper_format);
    SubGhzProtocolEncoderMercedes* instance = context;

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
       bit_count != MERCEDES_WIRE_BITS) {
        return SubGhzProtocolStatusErrorValueBitCount;
    }
    instance->generic.data_count_bit = (uint16_t)bit_count;

    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(
           flipper_format, MERCEDES_DATA_FIELD, instance->raw_data, MERCEDES_WIRE_BYTES) ||
       !mercedes_frame_valid(instance->raw_data)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    instance->generic.serial = mercedes_uid(instance->raw_data);
    instance->generic.btn = instance->raw_data[4];
    instance->generic.cnt = mercedes_counter(instance->raw_data);

    // [PROTOPIRATE_PORT] custom_btn support (replay-safe).
    // Mercedes button codes are not precisely known, so only OK is guaranteed to
    // replay the captured b4 byte-identically. The D-pad re-stamps b4 with a
    // best-effort code and refreshes the checksum b7 so the emitted frame still
    // round-trips through this decoder. set_max(4).
    const uint8_t original_btn = instance->raw_data[4];
    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(original_btn);
    }
    subghz_custom_btn_set_max(4);
    const uint8_t custom_btn_id = subghz_custom_btn_get();
    uint8_t new_btn = original_btn;
    switch(custom_btn_id) {
    case SUBGHZ_CUSTOM_BTN_UP:    new_btn = 0x01U; break; // best-effort: Lock
    case SUBGHZ_CUSTOM_BTN_DOWN:  new_btn = 0x02U; break; // best-effort: Unlock
    case SUBGHZ_CUSTOM_BTN_LEFT:  new_btn = 0x03U; break; // best-effort: Trunk
    case SUBGHZ_CUSTOM_BTN_RIGHT: new_btn = 0x04U; break; // best-effort: Aux
    case SUBGHZ_CUSTOM_BTN_OK:
    default:                      new_btn = original_btn; break; // exact replay
    }

    if(new_btn != original_btn) {
        instance->raw_data[4] = new_btn;
        // Refresh the sum-type checksum so the modified frame stays consistent.
        instance->raw_data[7] = mercedes_checksum(instance->raw_data);
        instance->generic.btn = new_btn;
    }

    instance->generic.data =
        ((uint64_t)instance->generic.serial << 16U) | instance->generic.cnt;
    instance->generic.data_count_bit = MERCEDES_WIRE_BITS;

    uint32_t repeat = MERCEDES_DEFAULT_REPEAT;
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Repeat", &repeat, 1);
    instance->encoder.repeat = (repeat == 0U) ? MERCEDES_DEFAULT_REPEAT : (size_t)repeat;

    if(instance->encoder.upload == NULL) {
        instance->encoder.upload = malloc(MERCEDES_UPLOAD_CAPACITY * sizeof(LevelDuration));
        furi_check(instance->encoder.upload);
    }
    if(!mercedes_encoder_build_upload(instance)) {
        return SubGhzProtocolStatusErrorEncoderGetUpload;
    }

    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

static const SubGhzProtocolEncoder enc_mercedes_vt = {
    .alloc = subghz_protocol_encoder_mercedes_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_mercedes_deserialize,
    .stop = subghz_protocol_encoder_mercedes_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_mercedes = {
    .name = MERCEDES_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 |
            SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_mercedes_vt,
};

static const SubGhzEncodePluginEntry enc_mercedes_entry = {.name = MERCEDES_PROTOCOL_NAME, .encoder = &enc_mercedes_vt};
static const FlipperAppPluginDescriptor enc_mercedes_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_mercedes_entry};
const FlipperAppPluginDescriptor* enc_mercedes_ep(void) { return &enc_mercedes_descriptor; }
