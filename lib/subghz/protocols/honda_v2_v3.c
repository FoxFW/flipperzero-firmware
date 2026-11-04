/**
 * @file honda_v2_v3.c
 * @brief Combined Honda V2/V3 SubGHz Protocol -- single "HondaV2&V3" protocol
 *
 * One protocol registration, variant detected at runtime from preamble timing.
 * V2: ~148 data bits, Manchester te_short=96us, preamble ~116x 1460us pulses
 * V3: 120-bit frame (15 bytes), Manchester te_short=250us, preamble ~230x 500us pulses
 *
 * Frequency: 433.920 MHz
 * Modulation: 2-FSK (FuriHalSubGhzPreset2FSKDev476Async)
 * Encoding: Manchester (custom LUT)
 */

#include "honda_v2_v3.h"

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#include <lib/subghz/blocks/custom_btn_i.h>
#include "common.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"

#define TAG "SubGhzProtocolHondaV2V3"

// ============================================================================
// TIMING CONSTANTS
// ============================================================================

// V2
#define HONDA_V2_TE_SHORT       96
#define HONDA_V2_TE_LONG        192
#define HONDA_V2_TE_DELTA       50
#define HONDA_V2_MIN_GAP        500
#define HONDA_V2_FRAME_BITS     148
#define HONDA_V2_MIN_FRAME_BITS 100
#define HONDA_V2_MAX_BITS       200
#define HONDA_V2_PREAMBLE_PULSE 1460
#define HONDA_V2_PREAMBLE_TOL   300
#define HONDA_V2_PREAMBLE_MIN   20
#define HONDA_V2_SYNC_BYTES     6
#define HONDA_V2_DATA_BYTES     13

static const SubGhzBlockConst subghz_protocol_honda_v2_const = {
    .te_short = HONDA_V2_TE_SHORT,
    .te_long = HONDA_V2_TE_LONG,
    .te_delta = HONDA_V2_TE_DELTA,
    .min_count_bit_for_found = HONDA_V2_MIN_FRAME_BITS,
};

// V3
#define HONDA_V3_TE_SHORT       250
#define HONDA_V3_TE_LONG        500
#define HONDA_V3_TE_DELTA       80
#define HONDA_V3_MIN_GAP        1500
#define HONDA_V3_FRAME_BITS     120
#define HONDA_V3_MIN_FRAME_BITS 100
#define HONDA_V3_MAX_BITS       200
#define HONDA_V3_PREAMBLE_PULSE 500
#define HONDA_V3_PREAMBLE_TOL   150
#define HONDA_V3_PREAMBLE_MIN   40
#define HONDA_V3_PREAMBLE_GAP   750
#define HONDA_V3_SYNC_BYTE      0x6A
#define HONDA_V3_TOTAL_BYTES    15

static const SubGhzBlockConst subghz_protocol_honda_v3_const = {
    .te_short = HONDA_V3_TE_SHORT,
    .te_long = HONDA_V3_TE_LONG,
    .te_delta = HONDA_V3_TE_DELTA,
    .min_count_bit_for_found = HONDA_V3_MIN_FRAME_BITS,
};

// V2 sync pattern bytes: 0x0E 0x1E 0x1E 0x1E 0x0D 0x6C
static const uint8_t honda_v2_sync_pattern[HONDA_V2_SYNC_BYTES] =
    {0x0E, 0x1E, 0x1E, 0x1E, 0x0D, 0x6C};

// ============================================================================
// MANCHESTER TABLE & SHARED DECODE
// ============================================================================

static const uint8_t honda_manchester_table[4] = {0x01, 0x91, 0x9B, 0xFB};

static bool honda_manchester_decode(uint8_t* state, uint8_t timing_type, uint8_t* bit_out) {
    if(timing_type == 8) {
        *state = 1;
        return false;
    }
    if(*state >= 4) {
        *state = 1;
        return false;
    }

    uint8_t entry = honda_manchester_table[*state];
    uint8_t new_state = (entry >> timing_type) & 0x3;

    if(*state == new_state) {
        return false;
    }

    *state = new_state;

    if(new_state == 2) {
        if(bit_out) *bit_out = 0;
        return true;
    } else if(new_state == 1) {
        if(bit_out) *bit_out = 1;
        return true;
    }

    return false;
}

// ============================================================================
// VARIANT ENUM & PARAMETERS TABLE
// ============================================================================

typedef enum {
    HondaVariantV2 = 2,
    HondaVariantV3 = 3,
} HondaVariant;

#define HONDA_VARIANT_IDX(v) ((v) == HondaVariantV2 ? 0 : 1)
#define HONDA_VARIANT_COUNT  2

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

static const HondaVariantParams* honda_get_params(HondaVariant v) {
    return &honda_variant_params[HONDA_VARIANT_IDX(v)];
}

// ============================================================================
// BUTTON CONSTANTS
// ============================================================================

// V2 buttons (btn_byte1, btn_byte2 pairs)
#define HONDA_V2_BTN1_LOCK       0x40
#define HONDA_V2_BTN2_LOCK       0x20
#define HONDA_V2_BTN1_UNLOCK     0x41
#define HONDA_V2_BTN2_UNLOCK     0x40
#define HONDA_V2_BTN1_UNLOCK_RTX 0x41
#define HONDA_V2_BTN2_UNLOCK_RTX 0x20
#define HONDA_V2_BTN1_TRUNK      0x40
#define HONDA_V2_BTN2_TRUNK      0x60

// V2 combined button codes for generic.btn (use btn_byte2 as primary identifier)
#define HONDA_V2_BTN_LOCK   0x20
#define HONDA_V2_BTN_UNLOCK 0x40
#define HONDA_V2_BTN_TRUNK  0x60

// V3 buttons (from byte 4 in Type A frames)
#define HONDA_V3_BTN_LOCK   0x8A
#define HONDA_V3_BTN_UNLOCK 0x8C

// ============================================================================
// UNIFIED BUTTON FUNCTIONS
// ============================================================================

static bool honda_v2_validate_buttons(uint8_t btn1, uint8_t btn2) {
    if(btn1 == HONDA_V2_BTN1_LOCK && btn2 == HONDA_V2_BTN2_LOCK) return true;
    if(btn1 == HONDA_V2_BTN1_UNLOCK && btn2 == HONDA_V2_BTN2_UNLOCK) return true;
    if(btn1 == HONDA_V2_BTN1_UNLOCK_RTX && btn2 == HONDA_V2_BTN2_UNLOCK_RTX) return true;
    if(btn1 == HONDA_V2_BTN1_TRUNK && btn2 == HONDA_V2_BTN2_TRUNK) return true;
    return false;
}

// ============================================================================
// DATA STRUCTURES
// ============================================================================

typedef enum {
    HondaDecoderStepReset = 0,
    HondaDecoderStepData = 1,
} HondaDecoderStep;

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    HondaVariant variant;
    uint8_t manchester_state;
    uint8_t bits[200];
    uint16_t bit_count;

    union {
        struct {
            uint32_t serial; // 24-bit serial
            uint8_t btn_byte1;
            uint8_t btn_byte2;
            uint16_t padding;
            uint8_t counter;
            uint32_t rolling;
            uint8_t tail;
            bool has_sync;
        } v2;
        struct {
            uint8_t sync; // should be 0x6A
            uint8_t counter;
            uint8_t payload[12]; // bytes 2-13
            uint8_t crc;
            uint16_t serial; // from Type A frames
            uint8_t btn_byte;
            uint8_t flags;
            bool is_fixed; // Type A vs Type B
        } v3;
    } extra;
} SubGhzProtocolDecoderHondaV2V3;

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

// ============================================================================
// PROTOCOL DEFINITION -- single VTable, single registration
// ============================================================================


const SubGhzProtocolDecoder subghz_protocol_honda_v2_v3_decoder = {
    .alloc = subghz_protocol_decoder_honda_v2_v3_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_honda_v2_v3_feed,
    .reset = subghz_protocol_decoder_honda_v2_v3_reset,
    .get_hash_data = subghz_protocol_decoder_honda_v2_v3_get_hash_data,
    .serialize = subghz_protocol_decoder_honda_v2_v3_serialize,
    .deserialize = subghz_protocol_decoder_honda_v2_v3_deserialize,
    .get_string = subghz_protocol_decoder_honda_v2_v3_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_honda_v2_v3_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_honda_v2_v3");

const SubGhzProtocol subghz_protocol_honda_v2_v3 = {
    .name = SUBGHZ_PROTOCOL_HONDA_V2_V3_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_honda_v2_v3_decoder,
    .encoder = &subghz_protocol_honda_v2_v3_encoder_ext.vtable,
};

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
// UNIFIED FRAME PROCESSING
// ============================================================================

static void honda_process_frame(SubGhzProtocolDecoderHondaV2V3* instance) {
    // Convert decoded bits to bytes
    uint16_t byte_count = (instance->bit_count + 7) / 8;
    uint8_t raw_bytes[32];
    memset(raw_bytes, 0, sizeof(raw_bytes));

    for(uint16_t i = 0; i < instance->bit_count && (i / 8) < sizeof(raw_bytes); i++) {
        if(instance->bits[i]) {
            raw_bytes[i / 8] |= (1 << (7 - (i % 8)));
        }
    }

    switch(instance->variant) {
    case HondaVariantV2: {
        // Find sync pattern in raw bytes
        int sync_offset = -1;
        if(byte_count >= HONDA_V2_SYNC_BYTES + HONDA_V2_DATA_BYTES) {
            for(uint16_t offset = 0;
                offset <= byte_count - HONDA_V2_SYNC_BYTES - HONDA_V2_DATA_BYTES;
                offset++) {
                bool match = true;
                for(uint8_t s = 0; s < HONDA_V2_SYNC_BYTES; s++) {
                    if(raw_bytes[offset + s] != honda_v2_sync_pattern[s]) {
                        match = false;
                        break;
                    }
                }
                if(match) {
                    sync_offset = offset;
                    break;
                }
            }
        }

        instance->extra.v2.has_sync = (sync_offset >= 0);

        uint8_t* d = NULL;
        if(sync_offset >= 0) {
            d = &raw_bytes[sync_offset + HONDA_V2_SYNC_BYTES];
        } else {
            // Fallback: try to find data without full sync
            if(byte_count >= HONDA_V2_DATA_BYTES) {
                // Skip leading preamble-like bytes and look for plausible data
                for(uint16_t offset = 0; offset <= byte_count - HONDA_V2_DATA_BYTES; offset++) {
                    uint8_t b1 = raw_bytes[offset + 3];
                    uint8_t b2 = raw_bytes[offset + 4];
                    if(honda_v2_validate_buttons(b1, b2)) {
                        d = &raw_bytes[offset];
                        break;
                    }
                }
            }
            if(!d) return;
        }

        // Extract 13 data bytes
        uint32_t serial = ((uint32_t)d[0] << 16) | ((uint32_t)d[1] << 8) | d[2];
        uint8_t btn1 = d[3];
        uint8_t btn2 = d[4];
        uint16_t padding = ((uint16_t)d[5] << 8) | d[6];
        uint8_t counter = d[7];
        uint32_t rolling = ((uint32_t)d[8] << 24) | ((uint32_t)d[9] << 16) |
                           ((uint32_t)d[10] << 8) | d[11];
        uint8_t tail = d[12];

        // Validate button codes
        if(!honda_v2_validate_buttons(btn1, btn2)) return;

        instance->extra.v2.serial = serial;
        instance->extra.v2.btn_byte1 = btn1;
        instance->extra.v2.btn_byte2 = btn2;
        instance->extra.v2.padding = padding;
        instance->extra.v2.counter = counter;
        instance->extra.v2.rolling = rolling;
        instance->extra.v2.tail = tail;

        honda_v2_pack_generic(&instance->generic, serial, counter, rolling, btn2);
    } break;

    case HondaVariantV3: {
        // Strip trailing 0x55 idle bytes (preamble leakage), but never
        // below frame size — CRC itself can be 0x55.
        while(byte_count > HONDA_V3_TOTAL_BYTES && raw_bytes[byte_count - 1] == 0x55) {
            byte_count--;
        }

        if(byte_count < HONDA_V3_TOTAL_BYTES) return;

        // Find sync byte 0x6A
        int sync_idx = -1;
        for(uint16_t i = 0; i <= byte_count - HONDA_V3_TOTAL_BYTES; i++) {
            if(raw_bytes[i] == HONDA_V3_SYNC_BYTE) {
                sync_idx = i;
                break;
            }
        }
        if(sync_idx < 0) return;

        uint8_t* d = &raw_bytes[sync_idx];

        uint8_t sync = d[0];
        uint8_t counter = d[1]; // raw byte: counter(6bit) << 2 | tx_flag(1) << 1 | bit0(1)
        uint8_t crc = d[14];

        /* Gate on the frame CRC. Previously an aligned 0x6A sync byte alone was
         * enough to accept a frame, so ~120-bit FSK noise containing a 0x6A
         * surfaced a bogus "Honda V3" with a false CRC:OK. */
        if(honda_v3_compute_crc(d) != crc) return;

        // Classify frame type: Type A (fixed) vs Type B (hopping)
        // Fixed frames: d[2..3]=serial, d[4]=button (0x8x), d[5]=flags (0x82/0x83)
        // Hopping frames: d[2..13] are fully encrypted, no readable button/serial
        uint8_t raw_btn = d[4];
        uint8_t flags = d[5];
        bool is_fixed = ((raw_btn & 0xF0) == 0x80) && (flags == 0x82 || flags == 0x83);

        uint8_t btn_byte = 0;
        uint16_t serial = 0;
        if(is_fixed) {
            serial = ((uint16_t)d[2] << 8) | d[3];
            btn_byte = raw_btn;
        }

        instance->extra.v3.sync = sync;
        instance->extra.v3.counter = counter;
        memcpy(instance->extra.v3.payload, &d[2], 12);
        instance->extra.v3.crc = crc;
        instance->extra.v3.serial = serial;
        instance->extra.v3.btn_byte = btn_byte;
        instance->extra.v3.flags = flags;
        instance->extra.v3.is_fixed = is_fixed;

        honda_v3_pack_generic(&instance->generic, serial, counter, btn_byte, crc);
        instance->generic.crc_check = true; // CRC validated above
    } break;

    default:
        return;
    }

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

// ============================================================================
// SHARED DECODE TIMING
// ============================================================================

static void
    honda_decode_timing(SubGhzProtocolDecoderHondaV2V3* instance, bool level, uint32_t duration) {
    const HondaVariantParams* params = honda_get_params(instance->variant);
    const SubGhzBlockConst* bc = params->block_const;

    uint8_t timing_type;
    bool valid = false;

    if(DURATION_DIFF(duration, bc->te_short) <= bc->te_delta) {
        timing_type = level ? 0 : 2;
        valid = true;
    } else if(DURATION_DIFF(duration, bc->te_long) <= bc->te_delta) {
        timing_type = level ? 4 : 6;
        valid = true;
    }

    if(valid) {
        uint8_t bit_value = 0;
        if(honda_manchester_decode(&instance->manchester_state, timing_type, &bit_value)) {
            if(instance->bit_count < params->max_bits) {
                instance->bits[instance->bit_count++] = bit_value;
            }

            if(instance->bit_count >= params->max_bits) {
                honda_process_frame(instance);
                instance->decoder.parser_step = HondaDecoderStepReset;
                instance->bit_count = 0;
                instance->manchester_state = 1;
            }
        }
    } else {
        if(instance->bit_count >= bc->min_count_bit_for_found) {
            honda_process_frame(instance);
        }
        instance->decoder.parser_step = HondaDecoderStepReset;
        instance->bit_count = 0;
        instance->manchester_state = 1;
    }
}

// ============================================================================
// DECODER -- ALLOC / FREE / RESET
// ============================================================================

void* subghz_protocol_decoder_honda_v2_v3_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);

    SubGhzProtocolDecoderHondaV2V3* instance = malloc(sizeof(SubGhzProtocolDecoderHondaV2V3));
    instance->base.protocol = &subghz_protocol_honda_v2_v3;
    instance->generic.protocol_name = subghz_protocol_honda_v2_v3.name;
    instance->variant = HondaVariantV2; // default, detected at runtime
    instance->manchester_state = 1;
    instance->bit_count = 0;
    memset(instance->bits, 0, sizeof(instance->bits));
    memset(&instance->extra, 0, sizeof(instance->extra));

    return instance;
}



void subghz_protocol_decoder_honda_v2_v3_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderHondaV2V3* instance = context;

    instance->decoder.parser_step = HondaDecoderStepReset;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->manchester_state = 1;
    instance->bit_count = 0;
    memset(&instance->extra, 0, sizeof(instance->extra));
}

// ============================================================================
// DECODER -- FEED (variant auto-detection from preamble/data timing)
// ============================================================================

void subghz_protocol_decoder_honda_v2_v3_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderHondaV2V3* instance = context;

    switch(instance->decoder.parser_step) {
    case HondaDecoderStepReset: {
        // Auto-detect variant from timing.
        // V2 preamble pulses are ~1460us, data Manchester uses te_short=96us
        // V3 preamble pulses are ~500us, data Manchester uses te_short=250us
        //
        // Strategy: Match incoming duration against both te_short values.
        // V2 te_short=96 vs V3 te_short=250 are well separated.
        // V2 preamble at 1460us won't match either te_short, so we'll
        // start collecting once we see actual data timings.

        int best_idx = -1;
        uint32_t best_diff = UINT32_MAX;

        for(int i = 0; i < HONDA_VARIANT_COUNT; i++) {
            const SubGhzBlockConst* bc = honda_variant_params[i].block_const;
            uint32_t diff = DURATION_DIFF(duration, bc->te_short);
            if(diff <= bc->te_delta && diff < best_diff) {
                best_diff = diff;
                best_idx = i;
            }
        }

        // Also check te_long for entry
        if(best_idx < 0) {
            for(int i = 0; i < HONDA_VARIANT_COUNT; i++) {
                const SubGhzBlockConst* bc = honda_variant_params[i].block_const;
                uint32_t diff = DURATION_DIFF(duration, bc->te_long);
                if(diff <= bc->te_delta && diff < best_diff) {
                    best_diff = diff;
                    best_idx = i;
                }
            }
        }

        if(best_idx >= 0) {
            instance->variant = honda_variant_params[best_idx].variant;
            instance->decoder.parser_step = HondaDecoderStepData;
            instance->manchester_state = 1;
            instance->bit_count = 0;
            memset(&instance->extra, 0, sizeof(instance->extra));
            honda_decode_timing(instance, level, duration);
        }
    } break;

    case HondaDecoderStepData: {
        const HondaVariantParams* params = honda_get_params(instance->variant);

        // Frame boundary: gap > min_gap
        if(duration > params->min_gap) {
            if(instance->bit_count >= params->block_const->min_count_bit_for_found) {
                honda_process_frame(instance);
            }
            instance->decoder.parser_step = HondaDecoderStepReset;
            instance->bit_count = 0;
            instance->manchester_state = 1;
            memset(&instance->extra, 0, sizeof(instance->extra));
            return;
        }

        honda_decode_timing(instance, level, duration);
    } break;

    default:
        instance->decoder.parser_step = HondaDecoderStepReset;
        break;
    }
}

// ============================================================================
// DECODER -- GET HASH DATA
// ============================================================================

uint8_t subghz_protocol_decoder_honda_v2_v3_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderHondaV2V3* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->bit_count >> 3) + 1);
}

// ============================================================================
// DECODER -- SERIALIZE
// ============================================================================

SubGhzProtocolStatus subghz_protocol_decoder_honda_v2_v3_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderHondaV2V3* dec = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&dec->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    // Write variant number
    uint32_t variant_temp = (uint32_t)dec->variant;
    if(!flipper_format_insert_or_update_uint32(flipper_format, "Variant", &variant_temp, 1)) {
        return SubGhzProtocolStatusError;
    }

    switch(dec->variant) {
    case HondaVariantV2: {
        uint32_t rolling_temp = dec->extra.v2.rolling;
        if(!flipper_format_insert_or_update_uint32(flipper_format, "Rolling", &rolling_temp, 1)) {
            return SubGhzProtocolStatusError;
        }
        uint32_t tail_temp = dec->extra.v2.tail;
        if(!flipper_format_insert_or_update_uint32(flipper_format, "Tail", &tail_temp, 1)) {
            return SubGhzProtocolStatusError;
        }
        uint32_t btn_hi_temp = dec->extra.v2.btn_byte1;
        if(!flipper_format_insert_or_update_uint32(flipper_format, "BtnHi", &btn_hi_temp, 1)) {
            return SubGhzProtocolStatusError;
        }
        uint32_t btn_lo_temp = dec->extra.v2.btn_byte2;
        if(!flipper_format_insert_or_update_uint32(flipper_format, "BtnLo", &btn_lo_temp, 1)) {
            return SubGhzProtocolStatusError;
        }
    } break;

    case HondaVariantV3: {
        uint32_t sync_temp = dec->extra.v3.sync;
        if(!flipper_format_insert_or_update_uint32(flipper_format, "Sync", &sync_temp, 1)) {
            return SubGhzProtocolStatusError;
        }
        uint32_t crc_temp = dec->extra.v3.crc;
        if(!flipper_format_insert_or_update_uint32(flipper_format, "CRC", &crc_temp, 1)) {
            return SubGhzProtocolStatusError;
        }
        uint32_t frame_type_temp = dec->extra.v3.is_fixed ? 1 : 0;
        if(!flipper_format_insert_or_update_uint32(
               flipper_format, "FrameType", &frame_type_temp, 1)) {
            return SubGhzProtocolStatusError;
        }
        uint32_t flags_temp = dec->extra.v3.flags;
        if(!flipper_format_insert_or_update_uint32(flipper_format, "Flags", &flags_temp, 1)) {
            return SubGhzProtocolStatusError;
        }
        // Save payload bytes as hex
        if(!flipper_format_insert_or_update_hex(
               flipper_format, "Payload", dec->extra.v3.payload, 12)) {
            return SubGhzProtocolStatusError;
        }
    } break;
    }

    return SubGhzProtocolStatusOk;
}

// ============================================================================
// DECODER -- DESERIALIZE
// ============================================================================

SubGhzProtocolStatus
    subghz_protocol_decoder_honda_v2_v3_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderHondaV2V3* dec = context;

    // Read generic data first (Key, Bit)
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize(&dec->generic, flipper_format);
    if(ret != SubGhzProtocolStatusOk) return ret;

    // Read variant -- or auto-detect from Bit count
    uint32_t variant_temp = 0;
    if(flipper_format_read_uint32(flipper_format, "Variant", &variant_temp, 1)) {
        dec->variant = (HondaVariant)variant_temp;
    } else {
        switch(dec->generic.data_count_bit) {
        case HONDA_V2_FRAME_BITS:
            dec->variant = HondaVariantV2;
            break;
        case HONDA_V3_FRAME_BITS:
            dec->variant = HondaVariantV3;
            break;
        default:
            return SubGhzProtocolStatusErrorValueBitCount;
        }
    }

    const HondaVariantParams* params = honda_get_params(dec->variant);

    // Validate bit count
    if(dec->generic.data_count_bit != params->frame_bits) {
        return SubGhzProtocolStatusErrorValueBitCount;
    }

    switch(dec->variant) {
    case HondaVariantV2: {
        uint32_t rolling_temp = 0;
        if(flipper_format_read_uint32(flipper_format, "Rolling", &rolling_temp, 1)) {
            dec->extra.v2.rolling = rolling_temp;
        } else {
            dec->extra.v2.rolling = 0;
        }

        uint32_t tail_temp = 0;
        if(flipper_format_read_uint32(flipper_format, "Tail", &tail_temp, 1)) {
            dec->extra.v2.tail = tail_temp & 0xFF;
        } else {
            dec->extra.v2.tail = 0;
        }

        uint32_t btn_hi_temp = 0;
        if(flipper_format_read_uint32(flipper_format, "BtnHi", &btn_hi_temp, 1)) {
            dec->extra.v2.btn_byte1 = btn_hi_temp & 0xFF;
        }

        uint32_t btn_lo_temp = 0;
        if(flipper_format_read_uint32(flipper_format, "BtnLo", &btn_lo_temp, 1)) {
            dec->extra.v2.btn_byte2 = btn_lo_temp & 0xFF;
        }

        // Unpack generic data
        uint32_t serial;
        uint8_t counter;
        uint32_t rolling;
        honda_v2_unpack_generic(&dec->generic, &serial, &counter, &rolling);
        dec->extra.v2.serial = serial;
        dec->extra.v2.counter = counter;
        if(dec->extra.v2.rolling == 0) {
            dec->extra.v2.rolling = rolling;
        }

        dec->generic.serial = serial;
        dec->generic.btn = dec->extra.v2.btn_byte2;
        dec->generic.cnt = counter;
    } break;

    case HondaVariantV3: {
        uint32_t sync_temp = 0;
        flipper_format_read_uint32(flipper_format, "Sync", &sync_temp, 1);
        dec->extra.v3.sync = sync_temp & 0xFF;

        uint32_t crc_temp = 0;
        flipper_format_read_uint32(flipper_format, "CRC", &crc_temp, 1);
        dec->extra.v3.crc = crc_temp & 0xFF;

        uint32_t frame_type_temp = 0;
        flipper_format_read_uint32(flipper_format, "FrameType", &frame_type_temp, 1);
        dec->extra.v3.is_fixed = (frame_type_temp == 1);

        uint32_t flags_temp = 0;
        flipper_format_read_uint32(flipper_format, "Flags", &flags_temp, 1);
        dec->extra.v3.flags = flags_temp & 0xFF;

        // Read payload
        uint8_t payload_buf[12] = {0};
        if(flipper_format_read_hex(flipper_format, "Payload", payload_buf, 12)) {
            memcpy(dec->extra.v3.payload, payload_buf, 12);
        }

        // Unpack generic data
        uint16_t serial;
        uint8_t counter;
        uint8_t btn_byte;
        uint8_t crc;
        honda_v3_unpack_generic(&dec->generic, &serial, &counter, &btn_byte, &crc);
        dec->extra.v3.serial = serial;
        dec->extra.v3.counter = counter;
        dec->extra.v3.btn_byte = btn_byte;
        if(dec->extra.v3.crc == 0) {
            dec->extra.v3.crc = crc;
        }

        dec->generic.serial = serial;
        dec->generic.btn = btn_byte;
        dec->generic.cnt = counter >> 2;
    } break;
    }

    subghz_custom_btn_set_original(dec->generic.btn);
    subghz_custom_btn_set_max(params->btn_max);

    return SubGhzProtocolStatusOk;
}

// ============================================================================
// DECODER -- GET STRING
// ============================================================================

void subghz_protocol_decoder_honda_v2_v3_get_string(void* context, FuriString* output) {
    SubGhzProtocolDecoderHondaV2V3* dec = context;
    const SubGhzDecodeInput input = {
        .decoder = context,
        .generic = &dec->generic,
        .extra = NULL,
        .extra_count = 0,
        .aux = NULL,
    };
    subghz_decode_plugin_get_string(dec->generic.protocol_name, &input, output);
}


// ============================================================================
// MANCHESTER ENCODER (shared, parametrized by te_short/te_long)
// ============================================================================

// Helper: Manchester-encode a byte array MSB-first
// ============================================================================
// UNIFIED BUILD UPLOAD
// ============================================================================

