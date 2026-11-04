/* enc_honda_v2_v3.fal : SD encoder for honda_v2_v3 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_honda_v2_v3_alloc enc_honda_v2_v3__subghz_protocol_encoder_honda_v2_v3_alloc
#define subghz_protocol_encoder_honda_v2_v3_deserialize enc_honda_v2_v3__subghz_protocol_encoder_honda_v2_v3_deserialize
#define subghz_protocol_encoder_honda_v2_v3_free enc_honda_v2_v3__subghz_protocol_encoder_honda_v2_v3_free
#define subghz_protocol_encoder_honda_v2_v3_stop enc_honda_v2_v3__subghz_protocol_encoder_honda_v2_v3_stop
#define subghz_protocol_encoder_honda_v2_v3_yield enc_honda_v2_v3__subghz_protocol_encoder_honda_v2_v3_yield
#define subghz_protocol_honda_v2_v3 enc_honda_v2_v3__subghz_protocol_honda_v2_v3
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/honda_v2_v3.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolHondaV2V3"
#define HONDA_V2_TE_SHORT       96
#define HONDA_V2_TE_LONG        192
#define HONDA_V2_TE_DELTA       50
#define HONDA_V2_MIN_GAP        500
#define HONDA_V2_FRAME_BITS     148
#define HONDA_V2_MIN_FRAME_BITS 100
#define HONDA_V2_MAX_BITS       200
#define HONDA_V2_PREAMBLE_PULSE 1460
#define HONDA_V2_SYNC_BYTES     6
#define HONDA_V2_DATA_BYTES     13
#define HONDA_V3_TE_SHORT       250
#define HONDA_V3_TE_LONG        500
#define HONDA_V3_TE_DELTA       80
#define HONDA_V3_MIN_GAP        1500
#define HONDA_V3_FRAME_BITS     120
#define HONDA_V3_MIN_FRAME_BITS 100
#define HONDA_V3_MAX_BITS       200
#define HONDA_V3_PREAMBLE_PULSE 500
#define HONDA_V3_PREAMBLE_GAP   750
#define HONDA_V3_SYNC_BYTE      0x6A
#define HONDA_V3_TOTAL_BYTES    15
#define HONDA_VARIANT_IDX(v) ((v) == HondaVariantV2 ? 0 : 1)
#define HONDA_VARIANT_COUNT  2
#define HONDA_V2_BTN1_LOCK       0x40
#define HONDA_V2_BTN2_LOCK       0x20
#define HONDA_V2_BTN1_UNLOCK     0x41
#define HONDA_V2_BTN2_UNLOCK     0x40
#define HONDA_V2_BTN1_TRUNK      0x40
#define HONDA_V2_BTN2_TRUNK      0x60
#define HONDA_V2_BTN_LOCK   0x20
#define HONDA_V2_BTN_UNLOCK 0x40
#define HONDA_V2_BTN_TRUNK  0x60
#define HONDA_V3_BTN_LOCK   0x8A
#define HONDA_V3_BTN_UNLOCK 0x8C

typedef enum {
    HondaVariantV2 = 2,
    HondaVariantV3 = 3,
} HondaVariant;

typedef struct {
    HondaVariant variant;
    const SubGhzBlockConst* block_const;
    uint32_t min_gap;
    uint16_t frame_bits;
    uint16_t max_bits;
    uint8_t btn_max;
    uint8_t cnt_length_bit;
    const char* display_name;
} HondaVariantParams;

typedef struct {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    HondaVariant variant;

    union {
        struct {
            uint32_t serial;
            uint8_t btn_byte1;
            uint8_t btn_byte2;
            uint16_t padding;
            uint8_t counter;
            uint32_t rolling;
            uint8_t tail;
        } v2;
        struct {
            uint8_t sync;
            uint8_t counter;
            uint8_t payload[12];
            uint8_t crc;
            uint16_t serial;
            uint8_t btn_byte;
            uint8_t flags;
            bool is_fixed;
        } v3;
    } extra;
} SubGhzProtocolEncoderHondaV2V3;

static const SubGhzBlockConst subghz_protocol_honda_v2_const = {
    .te_short = HONDA_V2_TE_SHORT,
    .te_long = HONDA_V2_TE_LONG,
    .te_delta = HONDA_V2_TE_DELTA,
    .min_count_bit_for_found = HONDA_V2_MIN_FRAME_BITS,
};

static const SubGhzBlockConst subghz_protocol_honda_v3_const = {
    .te_short = HONDA_V3_TE_SHORT,
    .te_long = HONDA_V3_TE_LONG,
    .te_delta = HONDA_V3_TE_DELTA,
    .min_count_bit_for_found = HONDA_V3_MIN_FRAME_BITS,
};

static const uint8_t honda_v2_sync_pattern[HONDA_V2_SYNC_BYTES] =
    {0x0E, 0x1E, 0x1E, 0x1E, 0x0D, 0x6C};

static const HondaVariantParams honda_variant_params[HONDA_VARIANT_COUNT] = {
    {
        .variant = HondaVariantV2,
        .block_const = &subghz_protocol_honda_v2_const,
        .min_gap = HONDA_V2_MIN_GAP,
        .frame_bits = HONDA_V2_FRAME_BITS,
        .max_bits = HONDA_V2_MAX_BITS,
        .btn_max = 3,
        .cnt_length_bit = 8,
        .display_name = "Honda V2",
    },
    {
        .variant = HondaVariantV3,
        .block_const = &subghz_protocol_honda_v3_const,
        .min_gap = HONDA_V3_MIN_GAP,
        .frame_bits = HONDA_V3_FRAME_BITS,
        .max_bits = HONDA_V3_MAX_BITS,
        .btn_max = 2,
        .cnt_length_bit = 6,
        .display_name = "Honda V3",
    },
};

static const HondaVariantParams* honda_get_params(HondaVariant v);
// ============================================================================
// UNIFIED BUTTON FUNCTIONS
// ============================================================================

static void honda_v2_btn_to_pair(uint8_t btn, uint8_t* btn1_out, uint8_t* btn2_out);
static uint8_t honda_get_btn_code(HondaVariant variant, uint8_t original_btn);
// ============================================================================
// V2 PACK / UNPACK helpers
// ============================================================================

static void honda_v2_pack_generic(
    SubGhzBlockGeneric* generic,
    uint32_t serial,
    uint8_t counter,
    uint32_t rolling,
    uint8_t btn_byte2);
static void honda_v2_unpack_generic(
    const SubGhzBlockGeneric* generic,
    uint32_t* serial_out,
    uint8_t* counter_out,
    uint32_t* rolling_out);
// ============================================================================
// V3 CRC: CRC-16 MSB poly=0x2627 on d[1..13], then 8-bit extraction via masks
// ============================================================================

static uint8_t honda_v3_compute_crc(const uint8_t* frame);
// ============================================================================
// V3 PACK / UNPACK helpers
// ============================================================================

static void honda_v3_pack_generic(
    SubGhzBlockGeneric* generic,
    uint16_t serial,
    uint8_t counter,
    uint8_t btn_byte,
    uint8_t crc);
static void honda_v3_unpack_generic(
    const SubGhzBlockGeneric* generic,
    uint16_t* serial_out,
    uint8_t* counter_out,
    uint8_t* btn_byte_out,
    uint8_t* crc_out);
// ============================================================================
// MANCHESTER ENCODER (shared, parametrized by te_short/te_long)
// ============================================================================

static size_t
    honda_encode_bit(LevelDuration* buf, uint8_t* state, uint8_t bit, uint16_t te_s, uint16_t te_l);
// Helper: Manchester-encode a byte array MSB-first
static size_t honda_encode_bytes(
    LevelDuration* buf,
    uint8_t* state,
    const uint8_t* data,
    uint16_t num_bytes,
    uint16_t te_s,
    uint16_t te_l);
// ============================================================================
// UNIFIED BUILD UPLOAD
// ============================================================================

static void honda_build_upload(SubGhzProtocolEncoderHondaV2V3* enc);

static const HondaVariantParams* honda_get_params(HondaVariant v) {
    return &honda_variant_params[HONDA_VARIANT_IDX(v)];
}

// ============================================================================
// UNIFIED BUTTON FUNCTIONS
// ============================================================================

static void honda_v2_btn_to_pair(uint8_t btn, uint8_t* btn1_out, uint8_t* btn2_out) {
    switch(btn) {
    case HONDA_V2_BTN_LOCK:
        if(btn1_out) *btn1_out = HONDA_V2_BTN1_LOCK;
        if(btn2_out) *btn2_out = HONDA_V2_BTN2_LOCK;
        break;
    case HONDA_V2_BTN_UNLOCK:
        if(btn1_out) *btn1_out = HONDA_V2_BTN1_UNLOCK;
        if(btn2_out) *btn2_out = HONDA_V2_BTN2_UNLOCK;
        break;
    case HONDA_V2_BTN_TRUNK:
        if(btn1_out) *btn1_out = HONDA_V2_BTN1_TRUNK;
        if(btn2_out) *btn2_out = HONDA_V2_BTN2_TRUNK;
        break;
    default:
        if(btn1_out) *btn1_out = HONDA_V2_BTN1_LOCK;
        if(btn2_out) *btn2_out = HONDA_V2_BTN2_LOCK;
        break;
    }
}

static uint8_t honda_get_btn_code(HondaVariant variant, uint8_t original_btn) {
    uint8_t custom_btn_id = subghz_custom_btn_get();
    if(custom_btn_id == SUBGHZ_CUSTOM_BTN_OK) return original_btn;

    switch(variant) {
    case HondaVariantV2:
        switch(custom_btn_id) {
        case SUBGHZ_CUSTOM_BTN_UP:
            return HONDA_V2_BTN_UNLOCK;
        case SUBGHZ_CUSTOM_BTN_DOWN:
            return HONDA_V2_BTN_LOCK;
        case SUBGHZ_CUSTOM_BTN_LEFT:
        case SUBGHZ_CUSTOM_BTN_RIGHT:
            return HONDA_V2_BTN_TRUNK;
        default:
            return original_btn;
        }
    case HondaVariantV3:
        switch(custom_btn_id) {
        case SUBGHZ_CUSTOM_BTN_UP:
            return HONDA_V3_BTN_UNLOCK;
        case SUBGHZ_CUSTOM_BTN_DOWN:
            return HONDA_V3_BTN_LOCK;
        default:
            return original_btn;
        }
    }
    return original_btn;
}

// ============================================================================
// V2 PACK / UNPACK helpers
// ============================================================================

static void honda_v2_pack_generic(
    SubGhzBlockGeneric* generic,
    uint32_t serial,
    uint8_t counter,
    uint32_t rolling,
    uint8_t btn_byte2) {
    // Pack: serial(24) in upper bits, counter(8), rolling(32)
    generic->data = ((uint64_t)(serial & 0xFFFFFF) << 40) | ((uint64_t)counter << 32) | rolling;
    generic->data_count_bit = HONDA_V2_FRAME_BITS;
    generic->serial = serial;
    generic->btn = btn_byte2; // use btn_byte2 as primary button identifier
    generic->cnt = counter;
}

static void honda_v2_unpack_generic(
    const SubGhzBlockGeneric* generic,
    uint32_t* serial_out,
    uint8_t* counter_out,
    uint32_t* rolling_out) {
    if(serial_out) *serial_out = (generic->data >> 40) & 0xFFFFFF;
    if(counter_out) *counter_out = (generic->data >> 32) & 0xFF;
    if(rolling_out) *rolling_out = (uint32_t)(generic->data & 0xFFFFFFFF);
}

// ============================================================================
// V3 CRC: CRC-16 MSB poly=0x2627 on d[1..13], then 8-bit extraction via masks
// ============================================================================

static uint8_t honda_v3_compute_crc(const uint8_t* frame) {
    // CRC-16 MSB-first over frame[1..13] (13 bytes)
    uint16_t crc = 0;
    for(int i = 1; i < 14; i++) {
        crc ^= (uint16_t)frame[i] << 8;
        for(int j = 0; j < 8; j++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x2627) : (uint16_t)(crc << 1);
        }
    }
    // Extract 8-bit CRC from 16-bit via parity masks
    static const uint16_t masks[8] = {
        0x8181, 0x4141, 0x2121, 0x80F2, 0xAF15, 0x0606, 0x0202, 0x0000};
    static const uint8_t constants = 0x1B; // bits: 00011011
    uint8_t result = 0;
    for(int b = 0; b < 8; b++) {
        uint16_t m = crc & masks[b];
        m ^= m >> 8;
        m ^= m >> 4;
        m ^= m >> 2;
        m ^= m >> 1;
        uint8_t parity = m & 1;
        result |= ((parity ^ ((constants >> (7 - b)) & 1)) << (7 - b));
    }
    return result;
}

// ============================================================================
// V3 PACK / UNPACK helpers
// ============================================================================

static void honda_v3_pack_generic(
    SubGhzBlockGeneric* generic,
    uint16_t serial,
    uint8_t counter,
    uint8_t btn_byte,
    uint8_t crc) {
    // Pack: serial(16) | btn_byte(8) | counter_raw(8) | crc(8) | padding(24)
    // counter raw byte: actual_counter(6bit) << 2 | tx_flag(1) << 1 | bit0(1)
    generic->data = ((uint64_t)(serial & 0xFFFF) << 48) | ((uint64_t)btn_byte << 40) |
                    ((uint64_t)counter << 32) | ((uint64_t)crc << 24);
    generic->data_count_bit = HONDA_V3_FRAME_BITS;
    generic->serial = serial;
    generic->btn = btn_byte;
    generic->cnt = counter >> 2;
    generic->crc = crc;
    /* crc_check is set by the caller only after a real CRC match, not here. */
}

static void honda_v3_unpack_generic(
    const SubGhzBlockGeneric* generic,
    uint16_t* serial_out,
    uint8_t* counter_out,
    uint8_t* btn_byte_out,
    uint8_t* crc_out) {
    if(serial_out) *serial_out = (generic->data >> 48) & 0xFFFF;
    if(counter_out) *counter_out = (generic->data >> 32) & 0xFF;
    if(btn_byte_out) *btn_byte_out = (generic->data >> 40) & 0xFF;
    if(crc_out) *crc_out = (generic->data >> 24) & 0xFF;
}

// ============================================================================
// MANCHESTER ENCODER (shared, parametrized by te_short/te_long)
// ============================================================================

static size_t
    honda_encode_bit(LevelDuration* buf, uint8_t* state, uint8_t bit, uint16_t te_s, uint16_t te_l) {
    size_t count = 0;

    switch(*state) {
    case 1:
        if(bit == 0) {
            buf[count++] = level_duration_make(false, (uint32_t)te_l);
            *state = 2;
        } else {
            buf[count++] = level_duration_make(false, (uint32_t)te_s);
            buf[count++] = level_duration_make(true, (uint32_t)te_s);
            *state = 1;
        }
        break;
    case 2:
        if(bit == 0) {
            buf[count++] = level_duration_make(true, (uint32_t)te_s);
            buf[count++] = level_duration_make(false, (uint32_t)te_s);
            *state = 2;
        } else {
            buf[count++] = level_duration_make(true, (uint32_t)te_l);
            *state = 1;
        }
        break;
    case 0:
        if(bit == 1) {
            buf[count++] = level_duration_make(true, (uint32_t)te_s);
            *state = 1;
        } else {
            buf[count++] = level_duration_make(true, (uint32_t)te_s);
            buf[count++] = level_duration_make(false, (uint32_t)te_l);
            *state = 2;
        }
        break;
    case 3:
        if(bit == 0) {
            buf[count++] = level_duration_make(false, (uint32_t)te_s);
            *state = 2;
        } else {
            buf[count++] = level_duration_make(false, (uint32_t)te_s);
            buf[count++] = level_duration_make(true, (uint32_t)te_l);
            *state = 1;
        }
        break;
    default:
        *state = 1;
        break;
    }

    return count;
}

// Helper: Manchester-encode a byte array MSB-first
static size_t honda_encode_bytes(
    LevelDuration* buf,
    uint8_t* state,
    const uint8_t* data,
    uint16_t num_bytes,
    uint16_t te_s,
    uint16_t te_l) {
    size_t idx = 0;
    for(uint16_t b = 0; b < num_bytes; b++) {
        for(int bit = 7; bit >= 0; bit--) {
            idx += honda_encode_bit(&buf[idx], state, (data[b] >> bit) & 1, te_s, te_l);
        }
    }
    return idx;
}

// ============================================================================
// UNIFIED BUILD UPLOAD
// ============================================================================

static void honda_build_upload(SubGhzProtocolEncoderHondaV2V3* enc) {
    LevelDuration* buf = enc->encoder.upload;
    size_t idx = 0;
    uint8_t state = 0;
    const HondaVariantParams* params = honda_get_params(enc->variant);
    uint16_t te_s = params->block_const->te_short;
    uint16_t te_l = params->block_const->te_long;

    switch(enc->variant) {
    case HondaVariantV2: {
        // V2: Preamble ~116 alternating pulses at 1460us
        for(int p = 0; p < 116; p++) {
            buf[idx++] = level_duration_make(p % 2 == 0, HONDA_V2_PREAMBLE_PULSE);
        }

        // Transition into data zone
        state = 0;

        // Encode sync pattern (6 bytes)
        idx += honda_encode_bytes(
            &buf[idx], &state, honda_v2_sync_pattern, HONDA_V2_SYNC_BYTES, te_s, te_l);

        // Encode 13 data bytes: serial(3) + btn(2) + pad(2) + counter(1) + rolling(4) + tail(1)
        uint8_t data[HONDA_V2_DATA_BYTES];
        data[0] = (enc->extra.v2.serial >> 16) & 0xFF;
        data[1] = (enc->extra.v2.serial >> 8) & 0xFF;
        data[2] = enc->extra.v2.serial & 0xFF;
        data[3] = enc->extra.v2.btn_byte1;
        data[4] = enc->extra.v2.btn_byte2;
        data[5] = (enc->extra.v2.padding >> 8) & 0xFF;
        data[6] = enc->extra.v2.padding & 0xFF;
        data[7] = enc->extra.v2.counter;
        data[8] = (enc->extra.v2.rolling >> 24) & 0xFF;
        data[9] = (enc->extra.v2.rolling >> 16) & 0xFF;
        data[10] = (enc->extra.v2.rolling >> 8) & 0xFF;
        data[11] = enc->extra.v2.rolling & 0xFF;
        data[12] = enc->extra.v2.tail;

        idx += honda_encode_bytes(&buf[idx], &state, data, HONDA_V2_DATA_BYTES, te_s, te_l);

        // Trailing gap
        buf[idx++] = level_duration_make(true, HONDA_V2_MIN_GAP);
    } break;

    case HondaVariantV3: {
        // V3: 2 bursts (TX then RTX)
        // Each burst: preamble + gap + data

        uint8_t frame[HONDA_V3_TOTAL_BYTES];
        frame[0] = enc->extra.v3.sync;
        frame[1] = enc->extra.v3.counter;
        memcpy(&frame[2], enc->extra.v3.payload, 12);
        frame[14] = honda_v3_compute_crc(frame);

        for(int burst = 0; burst < 2; burst++) {
            // Update counter for RTX burst
            if(burst == 1) {
                // RTX counter = TX counter - 2
                frame[1] = (enc->extra.v3.counter >= 2) ? (enc->extra.v3.counter - 2) : 0;
                frame[14] = honda_v3_compute_crc(frame);
            }

            // Preamble: ~230 alternating 500us pulses
            for(int p = 0; p < 230; p++) {
                buf[idx++] = level_duration_make(p % 2 == 0, HONDA_V3_PREAMBLE_PULSE);
            }

            // Gap before data
            buf[idx++] = level_duration_make(false, HONDA_V3_PREAMBLE_GAP);

            // Manchester encode 15 bytes (120 bits)
            state = 0;
            idx += honda_encode_bytes(&buf[idx], &state, frame, HONDA_V3_TOTAL_BYTES, te_s, te_l);

            // Flush last bit: if encoder ended high (state 1), the trailing gap
            // would merge with the last timing in roundtrip, losing the final bit.
            // A short low pulse separates them cleanly.
            if(state == 1) {
                buf[idx++] = level_duration_make(false, te_s);
            }

            // Trailing gap
            buf[idx++] = level_duration_make(true, HONDA_V3_MIN_GAP);
        }
    } break;
    }

    enc->encoder.size_upload = idx;
}

// ============================================================================
// ENCODER -- ALLOC / FREE / STOP / YIELD
// ============================================================================

void* subghz_protocol_encoder_honda_v2_v3_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);

    SubGhzProtocolEncoderHondaV2V3* instance = malloc(sizeof(SubGhzProtocolEncoderHondaV2V3));
    instance->base.protocol = &subghz_protocol_honda_v2_v3;
    instance->generic.protocol_name = subghz_protocol_honda_v2_v3.name;
    instance->variant = HondaVariantV2; // default, set during deserialize
    instance->encoder.repeat = 10;
    instance->encoder.size_upload = 1024;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    memset(&instance->extra, 0, sizeof(instance->extra));

    return instance;
}

void subghz_protocol_encoder_honda_v2_v3_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderHondaV2V3* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

void subghz_protocol_encoder_honda_v2_v3_stop(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderHondaV2V3* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_honda_v2_v3_yield(void* context) {
    SubGhzProtocolEncoderHondaV2V3* instance = context;

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

// ============================================================================
// ENCODER -- DESERIALIZE
// ============================================================================

SubGhzProtocolStatus
    subghz_protocol_encoder_honda_v2_v3_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderHondaV2V3* enc = context;

    enc->encoder.is_running = false;
    enc->encoder.front = 0;

    SubGhzProtocolStatus res = SubGhzProtocolStatusError;

    do {
        if(SubGhzProtocolStatusOk !=
           subghz_block_generic_deserialize(&enc->generic, flipper_format)) {
            FURI_LOG_E(TAG, "Deserialize error");
            break;
        }

        // Read variant -- or auto-detect from Bit count
        uint32_t variant_temp = 0;
        if(flipper_format_read_uint32(flipper_format, "Variant", &variant_temp, 1)) {
            enc->variant = (HondaVariant)variant_temp;
        } else {
            switch(enc->generic.data_count_bit) {
            case HONDA_V2_FRAME_BITS:
                enc->variant = HondaVariantV2;
                break;
            case HONDA_V3_FRAME_BITS:
                enc->variant = HondaVariantV3;
                break;
            default:
                FURI_LOG_E(TAG, "Unknown variant for bit count %d", enc->generic.data_count_bit);
                break;
            }
        }

        // Optional Repeat
        flipper_format_read_uint32(flipper_format, "Repeat", (uint32_t*)&enc->encoder.repeat, 1);

        switch(enc->variant) {
        case HondaVariantV2: {
            // Read extra fields
            uint32_t rolling_temp = 0;
            if(flipper_format_read_uint32(flipper_format, "Rolling", &rolling_temp, 1)) {
                enc->extra.v2.rolling = rolling_temp;
            }

            uint32_t tail_temp = 0;
            if(flipper_format_read_uint32(flipper_format, "Tail", &tail_temp, 1)) {
                enc->extra.v2.tail = tail_temp & 0xFF;
            }

            uint32_t btn_hi_temp = 0;
            if(flipper_format_read_uint32(flipper_format, "BtnHi", &btn_hi_temp, 1)) {
                enc->extra.v2.btn_byte1 = btn_hi_temp & 0xFF;
            }

            uint32_t btn_lo_temp = 0;
            if(flipper_format_read_uint32(flipper_format, "BtnLo", &btn_lo_temp, 1)) {
                enc->extra.v2.btn_byte2 = btn_lo_temp & 0xFF;
            }

            // Unpack generic
            uint32_t serial;
            uint8_t counter;
            uint32_t rolling;
            honda_v2_unpack_generic(&enc->generic, &serial, &counter, &rolling);
            enc->extra.v2.serial = serial;
            enc->extra.v2.counter = counter;
            if(enc->extra.v2.rolling == 0) {
                enc->extra.v2.rolling = rolling;
            }
            enc->extra.v2.padding = 0x0000;

            // Button handling
            if(subghz_custom_btn_get_original() == 0) {
                subghz_custom_btn_set_original(enc->extra.v2.btn_byte2);
            }
            subghz_custom_btn_set_max(3);

            {
                uint8_t btn = enc->extra.v2.btn_byte2;
                if(!subghz_block_generic_global_button_override_get(&btn)) {
                    uint8_t original = subghz_custom_btn_get_original();
                    btn = honda_get_btn_code(enc->variant, original);
                }
                enc->extra.v2.btn_byte2 = btn;
                // Update btn_byte1 to match
                honda_v2_btn_to_pair(btn, &enc->extra.v2.btn_byte1, NULL);
            }

            // Counter override (8-bit, +2/press)
            {
                uint32_t cnt32 = enc->extra.v2.counter;
                if(furi_hal_subghz_get_rolling_counter_mult() != -0x7FFFFFFF) {
                    if(!subghz_block_generic_global_counter_override_get(&cnt32)) {
                        if((cnt32 + furi_hal_subghz_get_rolling_counter_mult()) > 0xFF) {
                            cnt32 = 0;
                        } else {
                            cnt32 += furi_hal_subghz_get_rolling_counter_mult();
                        }
                    }
                } else {
                    cnt32 = (cnt32 + 2) & 0xFF;
                }
                enc->extra.v2.counter = cnt32 & 0xFF;
            }

            honda_v2_pack_generic(
                &enc->generic,
                enc->extra.v2.serial,
                enc->extra.v2.counter,
                enc->extra.v2.rolling,
                enc->extra.v2.btn_byte2);
        } break;

        case HondaVariantV3: {
            // Read extra fields
            uint32_t sync_temp = 0;
            flipper_format_read_uint32(flipper_format, "Sync", &sync_temp, 1);
            enc->extra.v3.sync = sync_temp & 0xFF;
            if(enc->extra.v3.sync == 0) enc->extra.v3.sync = HONDA_V3_SYNC_BYTE;

            uint32_t crc_temp = 0;
            flipper_format_read_uint32(flipper_format, "CRC", &crc_temp, 1);
            enc->extra.v3.crc = crc_temp & 0xFF;

            uint32_t frame_type_temp = 0;
            flipper_format_read_uint32(flipper_format, "FrameType", &frame_type_temp, 1);
            enc->extra.v3.is_fixed = (frame_type_temp == 1);

            uint32_t flags_temp = 0;
            flipper_format_read_uint32(flipper_format, "Flags", &flags_temp, 1);
            enc->extra.v3.flags = flags_temp & 0xFF;

            // Read payload
            uint8_t payload_buf[12] = {0};
            if(flipper_format_read_hex(flipper_format, "Payload", payload_buf, 12)) {
                memcpy(enc->extra.v3.payload, payload_buf, 12);
            }

            // Unpack generic
            uint16_t serial;
            uint8_t counter;
            uint8_t btn_byte;
            uint8_t crc;
            honda_v3_unpack_generic(&enc->generic, &serial, &counter, &btn_byte, &crc);
            enc->extra.v3.serial = serial;
            enc->extra.v3.counter = counter;
            enc->extra.v3.btn_byte = btn_byte;
            if(enc->extra.v3.crc == 0) {
                enc->extra.v3.crc = crc;
            }

            // Button handling
            if(subghz_custom_btn_get_original() == 0) {
                subghz_custom_btn_set_original(enc->extra.v3.btn_byte);
            }
            subghz_custom_btn_set_max(2);

            {
                uint8_t btn = enc->extra.v3.btn_byte;
                if(!subghz_block_generic_global_button_override_get(&btn)) {
                    uint8_t original = subghz_custom_btn_get_original();
                    btn = honda_get_btn_code(enc->variant, original);
                }
                enc->extra.v3.btn_byte = btn;
                // Update button in payload if fixed frame
                if(enc->extra.v3.is_fixed) {
                    enc->extra.v3.payload[2] = btn; // payload[2] = byte 4 = button
                }
            }

            // Counter override (8-bit, +4/press = 2 bursts x +2)
            // Raw counter byte: display_counter(6bit) << 2 | tx_bits(2)
            // So +1 display counter = +4 in raw byte
            {
                uint32_t cnt32 = enc->extra.v3.counter;
                if(furi_hal_subghz_get_rolling_counter_mult() != -0x7FFFFFFF) {
                    if(!subghz_block_generic_global_counter_override_get(&cnt32)) {
                        int32_t raw_delta =
                            (int32_t)furi_hal_subghz_get_rolling_counter_mult() * 4;
                        cnt32 = ((int32_t)cnt32 + raw_delta) & 0xFF;
                    }
                } else {
                    cnt32 = (cnt32 + 4) & 0xFF;
                }
                enc->extra.v3.counter = cnt32 & 0xFF;
            }

            honda_v3_pack_generic(
                &enc->generic,
                enc->extra.v3.serial,
                enc->extra.v3.counter,
                enc->extra.v3.btn_byte,
                enc->extra.v3.crc);
            enc->generic.crc_check = true; // frame rebuilt with computed CRC
        } break;
        }

        honda_build_upload(enc);

        // Write updated Key back
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }

        uint8_t key_data[sizeof(uint64_t)] = {0};
        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            key_data[sizeof(uint64_t) - i - 1] = (enc->generic.data >> (i * 8)) & 0xFF;
        }
        if(!flipper_format_update_hex(flipper_format, "Key", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Unable to update Key");
            break;
        }

        enc->encoder.is_running = true;
        res = SubGhzProtocolStatusOk;

    } while(false);

    return res;
}

static const SubGhzProtocolEncoder enc_honda_v2_v3_vt = {
    .alloc = subghz_protocol_encoder_honda_v2_v3_alloc,
    .free = subghz_protocol_encoder_honda_v2_v3_free,
    .deserialize = subghz_protocol_encoder_honda_v2_v3_deserialize,
    .stop = subghz_protocol_encoder_honda_v2_v3_stop,
    .yield = subghz_protocol_encoder_honda_v2_v3_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_honda_v2_v3 = {
    .name = SUBGHZ_PROTOCOL_HONDA_V2_V3_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_honda_v2_v3_vt,
};

static const SubGhzEncodePluginEntry enc_honda_v2_v3_entry = {.name = SUBGHZ_PROTOCOL_HONDA_V2_V3_NAME, .encoder = &enc_honda_v2_v3_vt};
static const FlipperAppPluginDescriptor enc_honda_v2_v3_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_honda_v2_v3_entry};
const FlipperAppPluginDescriptor* enc_honda_v2_v3_ep(void) { return &enc_honda_v2_v3_descriptor; }
