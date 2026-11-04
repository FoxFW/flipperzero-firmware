#include "compustar.h"

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include "../encode_plugin.h"

/*
 * Compustar 1WG3R / 1WAMR-1900 — car remote decoder
 *
 * Ported from the rtl_433 device driver "compustar_1wg3r"
 * (src/devices/compustar_1wg3r.c, Copyright (C) 2024 Ethan Halsall, GPLv2+).
 *
 * RF:
 *   - Modulation: OOK, Pulse Width Modulation (OOK_PULSE_PWM)
 *   - short pulse (mark) ~708us  => bit 1
 *   - long  pulse (mark) ~1076us => bit 0
 *   - sync_width ~1448us / reset_limit ~1532us: a gap this long ends the frame
 *   - Fixed code (no rolling counter)
 *
 * Frame layout (36 bits, MSB first):
 *
 *     IIIIIIIIIIIIIIII xxx bbbbbbbb iiiiiiii z
 *
 *   - I: 16-bit remote ID              (frame bits 0..15)
 *   - x: 3-bit unknown, always 0b111   (frame bits 16..18)
 *   - i: 8-bit inverted button code    (frame bits 19..26)
 *   - b: 8-bit button code             (frame bits 27..34)
 *   - z: 1-bit unknown, always 0       (frame bit 35)
 *
 *   Integrity (MIC): (~button_inverse & 0xFF) == button
 *   Sanity: ID != 0x0000 and ID != 0xFFFF; unknown field == 0b111
 *
 * feed() re-implements the rtl_433 PWM slicer as a pulse-duration state
 * machine: each mark is sliced to a bit by proximity to the short/long
 * width, a long gap flushes the accumulated frame, and the recovered 36-bit
 * word is validated with the MIC + sanity checks before it is emitted. Those
 * checks reject the burst-width false positives this codebase has fought.
 */

#define TAG "Compustar"

#define COMPUSTAR_TE_SHORT 708   /* short mark => bit 1 */
#define COMPUSTAR_TE_LONG  1076  /* long mark  => bit 0 */
#define COMPUSTAR_TE_DELTA 250   /* tolerance (~30%) */
#define COMPUSTAR_PULSE_MIN 400  /* reject marks shorter than this */
#define COMPUSTAR_PULSE_MAX 1330 /* reject marks longer than this (sync lives in the gap) */
#define COMPUSTAR_GAP_END  1400  /* gap >= this ends the frame (sync 1448 / reset 1532) */
#define COMPUSTAR_MID      ((COMPUSTAR_TE_SHORT + COMPUSTAR_TE_LONG) / 2) /* 892 */
#define COMPUSTAR_BIT_COUNT 36
#define COMPUSTAR_MAX_BITS 40

static const SubGhzBlockConst subghz_protocol_compustar_const = {
    .te_short = COMPUSTAR_TE_SHORT,
    .te_long = COMPUSTAR_TE_LONG,
    .te_delta = COMPUSTAR_TE_DELTA,
    .min_count_bit_for_found = COMPUSTAR_BIT_COUNT,
};

typedef enum {
    CompustarDecoderStepReset = 0,
    CompustarDecoderStepData,
} CompustarDecoderStep;

struct SubGhzProtocolDecoderCompustar {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderCompustar);


struct SubGhzProtocolEncoderCompustar {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderCompustar);


// ============================================================================
// Protocol registration
// ============================================================================

const SubGhzProtocolDecoder subghz_protocol_compustar_decoder = {
    .alloc = subghz_protocol_decoder_compustar_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_compustar_feed,
    .reset = subghz_protocol_decoder_compustar_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_compustar_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_compustar_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_compustar");

const SubGhzProtocol subghz_protocol_compustar = {
    .name = COMPUSTAR_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_compustar_decoder,
    .encoder = &subghz_protocol_compustar_encoder_ext.vtable,
};

// ============================================================================
// Button decoding (mirrors the rtl_433 button_map)
// ============================================================================

typedef struct {
    const char* name;
    uint8_t len;
    const uint8_t* vals;
} CompustarButton;

// ============================================================================
// Field extraction / validation
// ============================================================================

/* Parse generic.data (36-bit frame) into serial / btn / cnt. Returns true if
 * the MIC + sanity checks pass. */
static bool subghz_protocol_compustar_check(SubGhzBlockGeneric* instance) {
    if(instance->data_count_bit != COMPUSTAR_BIT_COUNT) return false;
    uint64_t data = instance->data & 0xFFFFFFFFFULL; /* 36 bits */

    uint16_t id = (uint16_t)((data >> 20) & 0xFFFF);
    if(id == 0x0000 || id == 0xFFFF) return false;

    /* 3-bit unknown field must be 0b111 */
    if(((data >> 17) & 0x7) != 0x7) return false;

    uint8_t button_inverse = (uint8_t)((data >> 9) & 0xFF);
    uint8_t button = (uint8_t)((data >> 1) & 0xFF);

    /* MIC: inverted button field must be the bitwise NOT of the button field */
    if((uint8_t)(~button_inverse) != button) return false;

    instance->serial = id;
    instance->btn = button;
    instance->cnt = 0; /* fixed code, no rolling counter */
    return true;
}

static void subghz_protocol_compustar_process_frame(SubGhzProtocolDecoderCompustar* instance) {
    uint16_t count = instance->decoder.decode_count_bit;

    /* rtl_433 accepts >= 35 bits; the final z bit is always 0, so normalise a
     * 35-bit capture up to the canonical 36-bit width by appending it. */
    if(count == 35) {
        instance->decoder.decode_data <<= 1;
        count = 36;
    }
    if(count != COMPUSTAR_BIT_COUNT) return;

    instance->generic.data = instance->decoder.decode_data & 0xFFFFFFFFFULL;
    instance->generic.data_count_bit = COMPUSTAR_BIT_COUNT;

    if(!subghz_protocol_compustar_check(&instance->generic)) return;

    FURI_LOG_D(
        TAG,
        "Id:%04lX Btn:%02X",
        (unsigned long)instance->generic.serial,
        instance->generic.btn);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

// ============================================================================
// Decoder: alloc / free / reset
// ============================================================================

void* subghz_protocol_decoder_compustar_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderCompustar* instance = malloc(sizeof(SubGhzProtocolDecoderCompustar));
    instance->base.protocol = &subghz_protocol_compustar;
    instance->generic.protocol_name = subghz_protocol_compustar.name;
    return instance;
}



void subghz_protocol_decoder_compustar_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderCompustar* instance = context;
    instance->decoder.parser_step = CompustarDecoderStepReset;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

// ============================================================================
// Decoder: feed — PWM slicer state machine
//
// Each mark (level == true) is sliced to a bit: closer to the short width => 1,
// closer to the long width => 0; a mark outside [PULSE_MIN, PULSE_MAX] is noise
// and resets the frame. A gap (level == false) >= GAP_END flushes the frame.
// ============================================================================

void subghz_protocol_decoder_compustar_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderCompustar* instance = context;

    switch(instance->decoder.parser_step) {
    case CompustarDecoderStepReset:
        if(level && duration >= COMPUSTAR_PULSE_MIN && duration <= COMPUSTAR_PULSE_MAX) {
            /* First data pulse: start a fresh frame */
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            subghz_protocol_blocks_add_bit(
                &instance->decoder, (duration < COMPUSTAR_MID) ? 1 : 0);
            instance->decoder.parser_step = CompustarDecoderStepData;
        }
        break;

    case CompustarDecoderStepData:
        if(level) {
            /* Mark: slice to a bit */
            if(duration < COMPUSTAR_PULSE_MIN || duration > COMPUSTAR_PULSE_MAX) {
                /* Out-of-range mark: noise, drop the frame */
                instance->decoder.parser_step = CompustarDecoderStepReset;
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                break;
            }
            if(instance->decoder.decode_count_bit >= COMPUSTAR_MAX_BITS) {
                /* Overlong burst: drop */
                instance->decoder.parser_step = CompustarDecoderStepReset;
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                break;
            }
            subghz_protocol_blocks_add_bit(
                &instance->decoder, (duration < COMPUSTAR_MID) ? 1 : 0);
        } else {
            /* Space: a long gap ends the frame */
            if(duration >= COMPUSTAR_GAP_END) {
                subghz_protocol_compustar_process_frame(instance);
                instance->decoder.parser_step = CompustarDecoderStepReset;
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
            }
            /* short gaps are the normal inter-pulse spacing: ignore */
        }
        break;
    }
}

// ============================================================================
// Decoder: hash / serialize / deserialize / string
// ============================================================================





SubGhzProtocolStatus
    subghz_protocol_decoder_compustar_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderCompustar* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_compustar_const.min_count_bit_for_found);
}

