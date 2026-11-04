#include "continental_rke.h"

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include "../encode_plugin.h"

/*
 * Continental RKE — Continental KR5V2X car remote keyfob decoder.
 *
 * Ported from the rtl_433 device "continental_car_remote"
 * (src/devices/continental_car_remote.c, merbanan/rtl_433).
 *
 * RF / demod (from the rtl_433 r_device):
 *   - Carrier ~313.8 MHz, 2-FSK  (FSK_PULSE_MANCHESTER_ZEROBIT)
 *   - Manchester-coded, half-bit ~100us, full-bit ~200us, reset gap 1500us
 *
 * Frame (after the 20-bit preamble 0xF0F06 == {0xF0,0xF0,0x60}[:20]):
 *   112 payload bits = 14 bytes, extracted MSB-first:
 *     bytes[0..3]   Remote ID (32-bit)
 *     bytes[4]      unknown
 *     bytes[5]>>4   Button (4-bit)
 *     bytes[6..8]   Sequence / rolling counter (24-bit)
 *     bytes[9..12]  Encrypted rolling code (32-bit)
 *     bytes[13]     XOR checksum
 *   Validation: xor_bytes(bytes, 14) == 0  (XOR of all 14 bytes is zero).
 *
 * Flipper feed() re-implements the rtl_433 manchester-zerobit pulse slicer as a
 * streaming state machine over LevelDuration pulses: it accumulates half-bit
 * durations, emits a bit on every data edge (falling edge = 1, rising = 0),
 * scans a rolling 20-bit window for the preamble, then collects 112 payload
 * bits and gates on the XOR checksum before emitting. The checksum gate is what
 * keeps this out of the false-positive territory the codebase has fought.
 *
 * Rolling-code / encrypted → decode + display only. Encoder is a stub (no TX).
 */

#define TAG "ContinentalRKE"

/* Timing, microseconds (rtl_433: short_width=100, long_width=200, reset=1500) */
#define CONTINENTAL_RKE_TE_SHORT   100
#define CONTINENTAL_RKE_TE_LONG    200
#define CONTINENTAL_RKE_TE_DELTA   40 /* ~+/-30-40% tolerance per symbol */
#define CONTINENTAL_RKE_TE_RESET   1500
/* Data-edge threshold: rtl_433 uses short_width * 1.5 */
#define CONTINENTAL_RKE_TE_THRESH  (CONTINENTAL_RKE_TE_SHORT + (CONTINENTAL_RKE_TE_SHORT / 2))
/* Upper bound on a single valid mark/space (a long symbol + tolerance) */
#define CONTINENTAL_RKE_TE_MAX     (CONTINENTAL_RKE_TE_LONG + CONTINENTAL_RKE_TE_DELTA)

/* 20-bit preamble: {0xF0,0xF0,0x60} first 20 bits = 0xF0F06 */
#define CONTINENTAL_RKE_PREAMBLE_20 0xF0F06UL
#define CONTINENTAL_RKE_PREAMBLE_MASK 0xFFFFFUL

#define CONTINENTAL_RKE_PAYLOAD_BYTES 14
#define CONTINENTAL_RKE_PAYLOAD_BITS  112 /* 14 bytes */
#define CONTINENTAL_RKE_KEY_BITS      64  /* MSB 8 bytes kept as the Key */

static const SubGhzBlockConst subghz_protocol_continental_rke_const = {
    .te_short = CONTINENTAL_RKE_TE_SHORT,
    .te_long = CONTINENTAL_RKE_TE_LONG,
    .te_delta = CONTINENTAL_RKE_TE_DELTA,
    .min_count_bit_for_found = CONTINENTAL_RKE_KEY_BITS,
};

// ============================================================================
// Instance state
// ============================================================================

struct SubGhzProtocolDecoderContinentalRke {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t te_accum; /* accumulated half-bit duration (rtl_433 time_since_last) */
    uint32_t preamble_shift; /* rolling 20-bit window while hunting the preamble */
    bool collecting; /* true once preamble matched, collecting payload bits */
    uint16_t frame_bit; /* payload bits collected so far */
    uint8_t frame[CONTINENTAL_RKE_PAYLOAD_BYTES];
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderContinentalRke);


struct SubGhzProtocolEncoderContinentalRke {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderContinentalRke);


// ============================================================================
// Protocol registration
// ============================================================================

const SubGhzProtocolDecoder subghz_protocol_continental_rke_decoder = {
    .alloc = subghz_protocol_decoder_continental_rke_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_continental_rke_feed,
    .reset = subghz_protocol_decoder_continental_rke_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_continental_rke_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_continental_rke_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_continental_rke");

const SubGhzProtocol subghz_protocol_continental_rke = {
    .name = CONTINENTAL_RKE_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_continental_rke_decoder,
    .encoder = &subghz_protocol_continental_rke_encoder_ext.vtable,
};

// ============================================================================
// Frame processing
// ============================================================================

static void
    subghz_protocol_continental_rke_process_frame(SubGhzProtocolDecoderContinentalRke* instance) {
    /* rtl_433: if (xor_bytes(bytes, 14)) return DECODE_FAIL_MIC; */
    uint8_t xor_sum = 0;
    for(size_t i = 0; i < CONTINENTAL_RKE_PAYLOAD_BYTES; i++) {
        xor_sum ^= instance->frame[i];
    }
    if(xor_sum != 0) {
        return; /* checksum fail — reject (prevents false positives) */
    }

    const uint8_t* b = instance->frame;

    uint32_t id = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) |
                  (uint32_t)b[3];
    uint8_t button = b[5] >> 4;
    uint32_t sequence = ((uint32_t)b[6] << 16) | ((uint32_t)b[7] << 8) | (uint32_t)b[8];
    uint32_t encrypted = ((uint32_t)b[9] << 24) | ((uint32_t)b[10] << 16) |
                         ((uint32_t)b[11] << 8) | (uint32_t)b[12];

    /* Keep the most-significant 64 bits (bytes 0..7 = ID + button + high counter)
     * as the stable Key for hashing / save / load. */
    uint64_t data = 0;
    for(size_t i = 0; i < 8; i++) {
        data = (data << 8) | (uint64_t)b[i];
    }

    instance->generic.data = data;
    instance->generic.data_count_bit = CONTINENTAL_RKE_KEY_BITS;
    instance->generic.serial = id;
    instance->generic.btn = button;
    instance->generic.cnt = sequence;
    instance->generic.seed = encrypted;

    /* Feed the block decoder so the hash covers the key. */
    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = CONTINENTAL_RKE_KEY_BITS;

    FURI_LOG_D(
        TAG,
        "ID:%08lX Btn:%X Cnt:%06lX Enc:%08lX",
        (unsigned long)id,
        button,
        (unsigned long)sequence,
        (unsigned long)encrypted);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

// ============================================================================
// Bit assembly: preamble hunt + payload collection
// ============================================================================

static void
    subghz_protocol_continental_rke_add_bit(SubGhzProtocolDecoderContinentalRke* instance, uint8_t bit) {
    bit &= 1;

    if(!instance->collecting) {
        instance->preamble_shift =
            ((instance->preamble_shift << 1) | bit) & CONTINENTAL_RKE_PREAMBLE_MASK;
        if(instance->preamble_shift == CONTINENTAL_RKE_PREAMBLE_20) {
            instance->collecting = true;
            instance->frame_bit = 0;
            for(size_t i = 0; i < CONTINENTAL_RKE_PAYLOAD_BYTES; i++) {
                instance->frame[i] = 0;
            }
        }
        return;
    }

    /* Store payload bits MSB-first into the byte buffer. */
    uint16_t idx = instance->frame_bit;
    if(bit) {
        instance->frame[idx >> 3] |= (uint8_t)(0x80U >> (idx & 7U));
    }
    instance->frame_bit++;

    if(instance->frame_bit >= CONTINENTAL_RKE_PAYLOAD_BITS) {
        subghz_protocol_continental_rke_process_frame(instance);
        /* Re-arm to hunt for the next preamble in the same burst. */
        instance->collecting = false;
        instance->preamble_shift = 0;
        instance->frame_bit = 0;
    }
}

static void
    subghz_protocol_continental_rke_restart(SubGhzProtocolDecoderContinentalRke* instance) {
    instance->te_accum = 0;
    instance->preamble_shift = 0;
    instance->collecting = false;
    instance->frame_bit = 0;
}

// ============================================================================
// Decoder: alloc / free / reset
// ============================================================================

void* subghz_protocol_decoder_continental_rke_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderContinentalRke* instance =
        malloc(sizeof(SubGhzProtocolDecoderContinentalRke));
    instance->base.protocol = &subghz_protocol_continental_rke;
    instance->generic.protocol_name = subghz_protocol_continental_rke.name;
    subghz_protocol_continental_rke_restart(instance);
    return instance;
}



void subghz_protocol_decoder_continental_rke_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderContinentalRke* instance = context;
    subghz_protocol_continental_rke_restart(instance);
}

// ============================================================================
// Decoder: feed
//
// Streaming re-implementation of rtl_433 pulse_slicer_manchester_zerobit.
// Each level==true duration is a mark (pulse), level==false is a space (gap).
// A half-symbol accumulates into te_accum; when accum+duration crosses the
// short*1.5 threshold a data edge occurred: falling edge (end of mark) = 1,
// rising edge (end of space) = 0. The prepended zero bit of the original
// slicer is unnecessary here because we hunt the preamble in a rolling window.
// ============================================================================

void subghz_protocol_decoder_continental_rke_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderContinentalRke* instance = context;

    /* Long silence ends the message / burst. */
    if(!level && duration >= CONTINENTAL_RKE_TE_RESET) {
        subghz_protocol_continental_rke_restart(instance);
        return;
    }

    /* Anything well outside a valid half/full symbol is noise — drop the row. */
    if(duration > CONTINENTAL_RKE_TE_MAX ||
       duration < (uint32_t)(CONTINENTAL_RKE_TE_SHORT - CONTINENTAL_RKE_TE_DELTA)) {
        subghz_protocol_continental_rke_restart(instance);
        return;
    }

    if(instance->te_accum + duration > CONTINENTAL_RKE_TE_THRESH) {
        /* Data edge: falling (end of mark) => 1, rising (end of space) => 0. */
        subghz_protocol_continental_rke_add_bit(instance, level ? 1 : 0);
        instance->te_accum = 0;
    } else {
        /* Half symbol — keep accumulating toward a full bit period. */
        instance->te_accum += duration;
    }
}

// ============================================================================
// Decoder: hash / serialize / deserialize / string
// ============================================================================





SubGhzProtocolStatus subghz_protocol_decoder_continental_rke_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderContinentalRke* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_continental_rke_const.min_count_bit_for_found);
}

