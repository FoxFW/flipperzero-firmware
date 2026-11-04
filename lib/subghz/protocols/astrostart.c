#include "astrostart.h"

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include "../encode_plugin.h"

/*
 * AstroStart 2000 — car remote start / keyfob decoder
 *
 * Ported from rtl_433 src/devices/astrostart_2000.c
 *
 * RF profile:
 *   - Carrier ~372.4 MHz (unusual band; tune manually — flagged 315/433 so it
 *     shows in the low-band frequency lists)
 *   - Modulation: OOK, Pulse Position Modulation (PPM)
 *   - Pulse (mark) roughly constant; the GAP after each pulse carries the bit:
 *       short gap ~326us  -> bit 0
 *       long  gap ~526us  -> bit 1
 *     rtl_433 slices at the short/long midpoint (426us) and ends the row at the
 *     reset limit (541us). Real inter-frame silences are far larger, so we widen
 *     the "1" window for jitter tolerance and only finalize on a clearly large
 *     gap.
 *
 * Frame: exactly 52 bits, MSB-first, packed as:
 *   bytes[0]      bits 0..7    button code
 *   bytes[1]      bits 8..15   ~button (bitwise inverse of bytes[0])
 *   bytes[2..5]   bits 16..47  32-bit remote ID (big-endian)
 *   bytes[6]>>4   bits 48..51  4-bit checksum
 *
 * Integrity (both must pass, else reject — kills false positives):
 *   1) bytes[0] == (~bytes[1] & 0xFF)
 *   2) checksum = sum of both nibbles of bytes[2..5], masked to 4 bits, each
 *      accumulation step masked to 4 bits, compared to bytes[6]>>4.
 */

#define TAG "Astrostart"

/* PPM gap timing (microseconds) */
#define ASTROSTART_TE_SHORT   326
#define ASTROSTART_TE_LONG    526
#define ASTROSTART_GAP_MID    426 /* (short+long)/2 : below = 0, above = 1 */
#define ASTROSTART_GAP_MIN    150 /* gaps shorter than this are noise */
#define ASTROSTART_GAP_MAX    750 /* gaps longer than this end the frame */
#define ASTROSTART_PULSE_MIN  120 /* sanity bounds on the mark/pulse width */
#define ASTROSTART_PULSE_MAX  900

#define ASTROSTART_FRAME_BITS 52

static const SubGhzBlockConst subghz_protocol_astrostart_const = {
    .te_short = ASTROSTART_TE_SHORT,
    .te_long = ASTROSTART_TE_LONG,
    .te_delta = 120,
    .min_count_bit_for_found = ASTROSTART_FRAME_BITS,
};

typedef enum {
    AstrostartDecoderStepReset = 0, /* waiting for a pulse to start */
    AstrostartDecoderStepCheckGap, /* got a pulse, expecting its gap */
} AstrostartDecoderStep;

struct SubGhzProtocolDecoderAstrostart {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderAstrostart);


struct SubGhzProtocolEncoderAstrostart {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderAstrostart);


// ============================================================================
// Registration
// ============================================================================

const SubGhzProtocolDecoder subghz_protocol_astrostart_decoder = {
    .alloc = subghz_protocol_decoder_astrostart_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_astrostart_feed,
    .reset = subghz_protocol_decoder_astrostart_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_astrostart_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_astrostart_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_astrostart");

const SubGhzProtocol subghz_protocol_astrostart = {
    .name = ASTROSTART_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_astrostart_decoder,
    .encoder = &subghz_protocol_astrostart_encoder_ext.vtable,
};

// ============================================================================
// Frame validation (rtl_433 parity)
// ============================================================================

/* Returns true and fills serial/btn/crc when the 52-bit frame passes both the
 * inverse-button check and the 4-bit checksum. */
static bool subghz_protocol_astrostart_check(
    uint64_t data,
    uint32_t* serial,
    uint8_t* btn,
    uint8_t* crc) {
    uint8_t bytes[7];
    bytes[0] = (uint8_t)(data >> 44); /* button          bits 0..7   */
    bytes[1] = (uint8_t)(data >> 36); /* ~button         bits 8..15  */
    bytes[2] = (uint8_t)(data >> 28); /* id[31..24]      bits 16..23 */
    bytes[3] = (uint8_t)(data >> 20); /* id[23..16]      bits 24..31 */
    bytes[4] = (uint8_t)(data >> 12); /* id[15..8]       bits 32..39 */
    bytes[5] = (uint8_t)(data >> 4); /* id[7..0]        bits 40..47 */
    uint8_t rx_checksum = (uint8_t)(data & 0x0F); /* checksum bits 48..51 */

    /* 1) button must be the bitwise inverse of the next byte */
    if(bytes[0] != (uint8_t)(~bytes[1] & 0xFF)) {
        return false;
    }

    /* 2) nibble-sum checksum over the four ID bytes */
    uint8_t expected = 0;
    for(int i = 2; i < 6; i++) {
        expected = (expected + (bytes[i] >> 4)) & 0x0F;
        expected = (expected + bytes[i]) & 0x0F;
    }
    if(expected != rx_checksum) {
        return false;
    }

    *serial = ((uint32_t)bytes[2] << 24) | ((uint32_t)bytes[3] << 16) |
              ((uint32_t)bytes[4] << 8) | (uint32_t)bytes[5];
    *btn = bytes[0];
    *crc = rx_checksum;
    return true;
}

/* Human-readable button name (matches the rtl_433 button_map). Rolling set of
 * six alternates per button plus a "Multiple" catch-all. */
static void subghz_protocol_astrostart_finalize(SubGhzProtocolDecoderAstrostart* instance) {
    if(instance->decoder.decode_count_bit != ASTROSTART_FRAME_BITS) return;

    uint32_t serial = 0;
    uint8_t btn = 0;
    uint8_t crc = 0;
    if(!subghz_protocol_astrostart_check(
           instance->decoder.decode_data, &serial, &btn, &crc)) {
        return;
    }

    instance->generic.data = instance->decoder.decode_data;
    instance->generic.data_count_bit = ASTROSTART_FRAME_BITS;
    instance->generic.serial = serial;
    instance->generic.btn = btn;
    instance->generic.cnt = 0; /* no rolling counter in this protocol */
    instance->generic.crc = crc;

    FURI_LOG_D(
        TAG, "Frame ID:%08lX Btn:%02X CRC:%X", (unsigned long)serial, btn, crc);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

// ============================================================================
// Decoder: alloc / free / reset
// ============================================================================

void* subghz_protocol_decoder_astrostart_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderAstrostart* instance =
        malloc(sizeof(SubGhzProtocolDecoderAstrostart));
    instance->base.protocol = &subghz_protocol_astrostart;
    instance->generic.protocol_name = subghz_protocol_astrostart.name;
    return instance;
}



void subghz_protocol_decoder_astrostart_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderAstrostart* instance = context;
    instance->decoder.parser_step = AstrostartDecoderStepReset;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

// ============================================================================
// Decoder: feed — PPM slicer over LevelDuration pulses
//
// Marks and spaces strictly alternate. A mark (level=true) is the pulse; the
// space (level=false) that follows carries one bit by its length. A large space
// (>= GAP_MAX) or an out-of-range mark ends the current frame.
// ============================================================================

void subghz_protocol_decoder_astrostart_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderAstrostart* instance = context;

    switch(instance->decoder.parser_step) {
    case AstrostartDecoderStepReset:
        if(level && duration >= ASTROSTART_PULSE_MIN && duration <= ASTROSTART_PULSE_MAX) {
            /* Valid leading pulse — begin collecting a fresh frame */
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = AstrostartDecoderStepCheckGap;
        }
        break;

    case AstrostartDecoderStepCheckGap:
        if(level) {
            /* Another pulse — validate its width, otherwise drop back to reset */
            if(duration < ASTROSTART_PULSE_MIN || duration > ASTROSTART_PULSE_MAX) {
                subghz_protocol_astrostart_finalize(instance);
                instance->decoder.parser_step = AstrostartDecoderStepReset;
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
            }
        } else {
            /* Gap — classify into a bit or finalize the frame */
            if(duration < ASTROSTART_GAP_MIN) {
                /* Runt gap: noise, abandon frame */
                instance->decoder.parser_step = AstrostartDecoderStepReset;
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
            } else if(duration >= ASTROSTART_GAP_MAX) {
                /* End of frame */
                subghz_protocol_astrostart_finalize(instance);
                instance->decoder.parser_step = AstrostartDecoderStepReset;
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
            } else {
                /* Data gap: short = 0, long = 1 */
                uint8_t bit = (duration < ASTROSTART_GAP_MID) ? 0 : 1;
                if(instance->decoder.decode_count_bit >= 64) {
                    /* Overflow guard — cannot be a valid 52-bit frame anymore */
                    instance->decoder.parser_step = AstrostartDecoderStepReset;
                    instance->decoder.decode_data = 0;
                    instance->decoder.decode_count_bit = 0;
                } else {
                    subghz_protocol_blocks_add_bit(&instance->decoder, bit);
                }
            }
        }
        break;
    }
}

// ============================================================================
// Decoder: hash / serialize / deserialize / string
// ============================================================================





SubGhzProtocolStatus
    subghz_protocol_decoder_astrostart_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderAstrostart* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_astrostart_const.min_count_bit_for_found);
}

