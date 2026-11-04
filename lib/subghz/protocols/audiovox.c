#include "audiovox.h"

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include "../encode_plugin.h"

/*
 * Audiovox PRO-OE3B Car Remote
 *
 * Ported from the rtl_433 decoder src/devices/audiovox_pro_oe3b.c
 * (Copyright (C) 2024 Ethan Halsall, GPLv2+).
 *
 * RF profile:
 *   - Carrier ~303.4 MHz (rtl_433 tunes 302.9 MHz), OOK
 *   - Modulation: OOK_PULSE_PWM  (fixed period ~1340us, the HIGH mark encodes the bit)
 *       short mark ~445us  -> bit 1
 *       long  mark ~895us  -> bit 0
 *   - Sync mark  ~1368us    -> frame anchor / start of a (repeated) frame
 *   - Reset gap  ~1790us    -> end of frame row
 *   - Frame length: exactly 25 bits, one row
 *
 * Field layout (bits are collected MSB-first exactly like rtl_433's bitbuffer):
 *   byte0 = bits  0..7      \ 16-bit ID (raw, pre-inversion)
 *   byte1 = bits  8..15     /
 *   byte2 = bits 16..23       button flags + fixed pattern
 *   bit24 = 25th bit          (present only; unused by the checks)
 *
 * rtl_433 has NO CRC on this remote; it validates with sanity checks. We
 * replicate them exactly (they are what keeps the false-positive rate down):
 *   1) On the RAW (non-inverted) byte2:  reject if (byte2 & 0xAA) || byte2 == 0x55
 *   2) Invert the whole frame.
 *   3) id = (byte0 << 8) | byte1 ;  reject if id == 0 or id == 0xFFFF
 *   4) At least one button bit must be set in inverted byte2 at masks
 *      0x01 (Lock), 0x04 (Unlock), 0x10 (Option), 0x40 (Trunk); else reject.
 *
 * This is a receive/decode-only port (fixed-code remote). The encoder is a
 * stub — the signal loads and saves but is not re-transmitted.
 */

#define TAG "Audiovox"

#define AUDIOVOX_TE_SHORT 445
#define AUDIOVOX_TE_LONG  895
#define AUDIOVOX_TE_SYNC  1368
#define AUDIOVOX_TE_RESET 1790

/* Nearest-neighbour classification midpoints (~+/-30% tolerant windows). */
#define AUDIOVOX_MARK_MIN      150 /* below this a mark is noise */
#define AUDIOVOX_SHORT_LONG_MID 670 /* (445+895)/2 */
#define AUDIOVOX_LONG_SYNC_MID 1131 /* (895+1368)/2 */
#define AUDIOVOX_SYNC_MAX      1600 /* above this a mark is over-long */
#define AUDIOVOX_GAP_RESET     1300 /* gap >= this ends the frame row */

#define AUDIOVOX_FRAME_BITS 25
#define AUDIOVOX_MAX_BITS   40 /* runaway guard */

static const SubGhzBlockConst subghz_protocol_audiovox_const = {
    .te_short = AUDIOVOX_TE_SHORT,
    .te_long = AUDIOVOX_TE_LONG,
    .te_delta = 180,
    .min_count_bit_for_found = AUDIOVOX_FRAME_BITS,
};

typedef enum {
    AudiovoxDecoderStepReset = 0,
    AudiovoxDecoderStepData,
} AudiovoxDecoderStep;

struct SubGhzProtocolDecoderAudiovox {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};


struct SubGhzProtocolEncoderAudiovox {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};


typedef struct SubGhzProtocolDecoderAudiovox SubGhzProtocolDecoderAudiovox;
typedef struct SubGhzProtocolEncoderAudiovox SubGhzProtocolEncoderAudiovox;

// ============================================================================
// Forward declarations
// ============================================================================

static void* subghz_protocol_decoder_audiovox_alloc(SubGhzEnvironment* environment);
static void subghz_protocol_decoder_audiovox_reset(void* context);
static void subghz_protocol_decoder_audiovox_feed(void* context, bool level, uint32_t duration);
static SubGhzProtocolStatus
    subghz_protocol_decoder_audiovox_deserialize(void* context, FlipperFormat* flipper_format);


// ============================================================================
// Protocol registration
// ============================================================================

const SubGhzProtocolDecoder subghz_protocol_audiovox_decoder = {
    .alloc = subghz_protocol_decoder_audiovox_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_audiovox_feed,
    .reset = subghz_protocol_decoder_audiovox_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_audiovox_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_audiovox_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_audiovox");

const SubGhzProtocol subghz_protocol_audiovox = {
    .name = AUDIOVOX_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_audiovox_decoder,
    .encoder = &subghz_protocol_audiovox_encoder_ext.vtable,
};

// ============================================================================
// Frame extraction helpers (shared by decode + display)
// ============================================================================

typedef struct {
    uint16_t id;
    uint8_t btn_bits; /* inverted byte2 masked to the four button positions */
} AudiovoxFrame;

/*
 * Reconstruct/validate a 25-bit frame held MSB-first in `data`.
 * Returns true and fills `out` when all rtl_433 sanity checks pass.
 */
static bool subghz_protocol_audiovox_parse(uint64_t data, AudiovoxFrame* out) {
    /* MSB-first collection: first received bit sits at bit24. */
    uint8_t b0 = (uint8_t)((data >> 17) & 0xFF); /* bits  0..7  */
    uint8_t b1 = (uint8_t)((data >> 9) & 0xFF); /* bits  8..15 */
    uint8_t b2 = (uint8_t)((data >> 1) & 0xFF); /* bits 16..23 */

    /* (1) sanity on the RAW byte2 */
    if((b2 & 0xAA) || (b2 == 0x55)) {
        return false;
    }

    /* (2) invert the frame */
    uint8_t i0 = (uint8_t)~b0;
    uint8_t i1 = (uint8_t)~b1;
    uint8_t i2 = (uint8_t)~b2;

    /* (3) id sanity */
    uint16_t id = (uint16_t)((i0 << 8) | i1);
    if(id == 0 || id == 0xFFFF) {
        return false;
    }

    /* (4) at least one button flag set */
    int matches = 0;
    uint8_t mask = 0x01;
    for(int i = 0; i < 4; i++) {
        if(i2 & mask) matches++;
        mask <<= 2;
    }
    if(!matches) {
        return false;
    }

    out->id = id;
    out->btn_bits = i2 & 0x55;
    return true;
}

// ============================================================================
// Process a completed frame
// ============================================================================

static void subghz_protocol_audiovox_process(SubGhzProtocolDecoderAudiovox* instance) {
    if(instance->decoder.decode_count_bit != AUDIOVOX_FRAME_BITS) {
        return;
    }

    AudiovoxFrame frame;
    if(!subghz_protocol_audiovox_parse(instance->decoder.decode_data, &frame)) {
        return;
    }

    instance->generic.data = instance->decoder.decode_data;
    instance->generic.data_count_bit = instance->decoder.decode_count_bit;
    instance->generic.serial = frame.id;
    instance->generic.btn = frame.btn_bits;
    instance->generic.cnt = 0;

    FURI_LOG_D(TAG, "Frame: ID:%04X Btn:%02X", frame.id, frame.btn_bits);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

// ============================================================================
// Decoder: alloc / free / reset
// ============================================================================

static void* subghz_protocol_decoder_audiovox_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderAudiovox* instance = malloc(sizeof(SubGhzProtocolDecoderAudiovox));
    instance->base.protocol = &subghz_protocol_audiovox;
    instance->generic.protocol_name = subghz_protocol_audiovox.name;
    return instance;
}



static void subghz_protocol_decoder_audiovox_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderAudiovox* instance = context;
    instance->decoder.parser_step = AudiovoxDecoderStepReset;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

// ============================================================================
// Decoder: feed — PWM pulse-duration state machine
//
// Re-implements rtl_433's OOK_PULSE_PWM slicer for this remote. Each data bit
// is one HIGH mark: short (~445us) => 1, long (~895us) => 0 (rtl_433 short=1
// convention). A sync mark (~1368us) anchors the frame start and separates
// repeated copies; a gap >= reset ends the row. On row end we require exactly
// 25 bits and run the rtl_433 sanity checks before emitting.
// ============================================================================

static void subghz_protocol_decoder_audiovox_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderAudiovox* instance = context;

    switch(instance->decoder.parser_step) {
    case AudiovoxDecoderStepReset:
        /* Wait for a sync mark to anchor the frame. */
        if(level && duration >= AUDIOVOX_LONG_SYNC_MID && duration < AUDIOVOX_SYNC_MAX) {
            instance->decoder.parser_step = AudiovoxDecoderStepData;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
        }
        break;

    case AudiovoxDecoderStepData:
        if(level) {
            /* HIGH mark: classify by width. */
            if(duration < AUDIOVOX_MARK_MIN) {
                /* Noise glitch: abandon this frame. */
                instance->decoder.parser_step = AudiovoxDecoderStepReset;
            } else if(duration < AUDIOVOX_SHORT_LONG_MID) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1); /* short = 1 */
            } else if(duration < AUDIOVOX_LONG_SYNC_MID) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0); /* long = 0 */
            } else if(duration < AUDIOVOX_SYNC_MAX) {
                /* New sync mark: a repeated frame begins. Emit the one we have
                 * (if complete), then start collecting the next copy. */
                subghz_protocol_audiovox_process(instance);
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
            } else {
                /* Over-long mark: end of transmission. */
                subghz_protocol_audiovox_process(instance);
                instance->decoder.parser_step = AudiovoxDecoderStepReset;
            }

            /* Runaway guard against corrupted streams. */
            if(instance->decoder.decode_count_bit > AUDIOVOX_MAX_BITS) {
                instance->decoder.parser_step = AudiovoxDecoderStepReset;
            }
        } else {
            /* Gap: only a reset-length gap is meaningful (ends the row). */
            if(duration >= AUDIOVOX_GAP_RESET) {
                subghz_protocol_audiovox_process(instance);
                instance->decoder.parser_step = AudiovoxDecoderStepReset;
            }
            /* shorter gaps are the inter-bit spacing: ignore */
        }
        break;
    }
}

// ============================================================================
// Decoder: hash / serialize / deserialize / get_string
// ============================================================================





static SubGhzProtocolStatus
    subghz_protocol_decoder_audiovox_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderAudiovox* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_audiovox_const.min_count_bit_for_found);
}

// ============================================================================
// Encoder stubs (fixed-code remote; TX not implemented — load/save only)
// ============================================================================

/* Layout asserts placed at EOF: these structs typedef the bare name
 * later in this file, so the assert must follow all type definitions. */
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderAudiovox);
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderAudiovox);
