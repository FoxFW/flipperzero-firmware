#include "opel_v0.h"

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include "../encode_plugin.h"

/*
 * Opel V0 — Opel/GM keyfob decoder (experimental)
 *
 * Signal structure (433.92 MHz, OOK 650):
 *   - Carrier frequency ~1200 Hz (period ~830us per carrier cycle)
 *   - Carrier grouped into bursts of ~4 cycles (~3500us) separated by ~5400us gaps
 *   - Data encoded in burst width: long burst (~3500us) vs short burst (~2500us)
 *   - Preamble: continuous carrier >50000us
 *
 * Decoder approach:
 *   - Merge marks separated by tiny spaces (<120us) into carrier bursts
 *   - Classify bursts by total width: >threshold = 1, <=threshold = 0
 *   - Collect bits until gap >20000us (frame end)
 *
 * NOTE: This is a first-pass decoder. The exact encoding is not fully understood.
 *       The bit threshold and frame structure need refinement with more signal samples.
 */

#define TAG "OpelV0"

/* Timing constants derived from signal analysis */
#define OPEL_V0_CARRIER_PERIOD  830   /* single carrier cycle (mark + space) */
#define OPEL_V0_MERGE_THRESHOLD 120   /* spaces below this are within a carrier burst */
#define OPEL_V0_GAP_MIN         4000  /* minimum gap between bursts */
#define OPEL_V0_GAP_MAX         7000  /* maximum gap between bursts */
#define OPEL_V0_PREAMBLE_MIN    50000 /* minimum preamble carrier duration */
#define OPEL_V0_BIT_THRESHOLD   3200  /* burst width threshold: >3200 = 1, <=3200 = 0 */
#define OPEL_V0_MIN_BITS        40    /* minimum bits for a valid frame */
#define OPEL_V0_MAX_BITS        150   /* maximum bits to collect */
#define OPEL_V0_FRAME_GAP       20000 /* gap that ends a frame */

/* Structural gate (false-positive rejection). The classifier emits on burst-width
 * structure alone; require a fixed modal frame length instead of the wide 40..96
 * window, and reject degenerate all-zero / all-one payloads. */
#define OPEL_V0_FRAME_BITS      64    /* modal captured frame length */
#define OPEL_V0_FRAME_DELTA     2     /* tolerance around modal (quantization jitter) */

static const SubGhzBlockConst subghz_protocol_opel_v0_const = {
    .te_short = 830,
    .te_long = 5400,
    .te_delta = 300,
    .min_count_bit_for_found = OPEL_V0_MIN_BITS,
};

// ============================================================================
// Decoder state machine
// ============================================================================

typedef enum {
    OpelV0DecoderStepReset = 0,
    OpelV0DecoderStepPreamble,
    OpelV0DecoderStepData,
} OpelV0DecoderStep;

struct SubGhzProtocolDecoderOpelV0 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t current_burst;  /* accumulated mark width for current burst */
    uint32_t preamble_len;   /* accumulated preamble carrier length */
    uint16_t bit_count;      /* bits collected so far */

    /* Store up to MAX_BITS burst widths for analysis */
    uint16_t burst_widths[OPEL_V0_MAX_BITS];
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderOpelV0);


struct SubGhzProtocolEncoderOpelV0 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderOpelV0);


// ============================================================================
// Protocol registration
// ============================================================================

#ifdef SUBGHZ_DECODER_TEST
#endif

const SubGhzProtocolDecoder subghz_protocol_opel_v0_decoder = {
    .alloc = subghz_protocol_decoder_opel_v0_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_opel_v0_feed,
    .reset = subghz_protocol_decoder_opel_v0_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_opel_v0_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
#ifdef SUBGHZ_DECODER_TEST
#endif
};

static const SubGhzEncoderExternal subghz_protocol_opel_v0_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_opel_v0");

const SubGhzProtocol subghz_protocol_opel_v0 = {
    .name = SUBGHZ_PROTOCOL_OPEL_V0_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_opel_v0_decoder,
    .encoder = &subghz_protocol_opel_v0_encoder_ext.vtable,
};

// ============================================================================
// Process a completed frame
// ============================================================================

static void subghz_protocol_opel_v0_process_frame(SubGhzProtocolDecoderOpelV0* instance) {
    if(instance->bit_count < OPEL_V0_MIN_BITS) return;

    /* Classify burst widths into bits */
    uint64_t data = 0;
    uint32_t data_2 = 0;
    uint16_t count = instance->bit_count;
    if(count > 96) count = 96;

    for(uint16_t i = 0; i < count; i++) {
        uint8_t bit = (instance->burst_widths[i] > OPEL_V0_BIT_THRESHOLD) ? 1 : 0;
        if(i < 64) {
            data = (data << 1) | bit;
        } else {
            data_2 = (data_2 << 1) | bit;
        }
    }

    /* Structural gate: require the modal fixed frame length (reject the wide
     * 40..96 window) and reject degenerate all-zero / all-one data. Emitting on
     * width-threshold structure alone produced the false positives. */
    if(count < (OPEL_V0_FRAME_BITS - OPEL_V0_FRAME_DELTA) ||
       count > (OPEL_V0_FRAME_BITS + OPEL_V0_FRAME_DELTA)) {
        return;
    }
    {
        uint64_t used_mask =
            (count >= 64) ? 0xFFFFFFFFFFFFFFFFULL : ((1ULL << count) - 1ULL);
        bool all_zero = (data == 0) && (data_2 == 0);
        bool all_one = (data == used_mask) &&
                       (count <= 64 || data_2 == ((1UL << (count - 64)) - 1UL));
        if(all_zero || all_one) {
            return;
        }
    }

    /* Pad data to 64 bits if shorter */
    if(count < 64) {
        data <<= (64 - count);
    }

    instance->generic.data = data;
    instance->generic.data_count_bit = count;

    /* Store extra bits in seed field */
    if(count > 64) {
        uint8_t extra = count - 64;
        data_2 <<= (32 - extra);
        instance->generic.seed = data_2;
    } else {
        instance->generic.seed = 0;
    }

    FURI_LOG_D(TAG, "Frame: %d bits, Key:%016llX", count, data);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

// ============================================================================
// Decoder: alloc / free / reset
// ============================================================================

void* subghz_protocol_decoder_opel_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderOpelV0* instance = malloc(sizeof(SubGhzProtocolDecoderOpelV0));
    instance->base.protocol = &subghz_protocol_opel_v0;
    instance->generic.protocol_name = subghz_protocol_opel_v0.name;
    return instance;
}



void subghz_protocol_decoder_opel_v0_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderOpelV0* instance = context;
    instance->decoder.parser_step = OpelV0DecoderStepReset;
    instance->current_burst = 0;
    instance->preamble_len = 0;
    instance->bit_count = 0;
}

// ============================================================================
// Decoder: feed
//
// Strategy: accumulate mark durations. Spaces < MERGE_THRESHOLD are added to
// the current burst (they're intra-carrier gaps). Spaces in the GAP range
// (4000-7000us) terminate one burst and start the next. Spaces > FRAME_GAP
// terminate the frame.
// ============================================================================

void subghz_protocol_decoder_opel_v0_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderOpelV0* instance = context;

    switch(instance->decoder.parser_step) {
    case OpelV0DecoderStepReset:
        if(level) {
            /* Start accumulating carrier */
            instance->current_burst = duration;
            instance->preamble_len = 0;
            instance->bit_count = 0;
            instance->decoder.parser_step = OpelV0DecoderStepPreamble;
        }
        break;

    case OpelV0DecoderStepPreamble:
        if(level) {
            /* Mark: add to current burst */
            instance->current_burst += duration;
        } else {
            /* Space */
            if(duration < OPEL_V0_MERGE_THRESHOLD) {
                /* Tiny space: within carrier cycle, merge */
                instance->current_burst += duration;
            } else if(duration < OPEL_V0_GAP_MAX) {
                /* Medium gap: end of a carrier burst */
                instance->preamble_len += instance->current_burst;

                if(instance->preamble_len >= OPEL_V0_PREAMBLE_MIN) {
                    /* Preamble found, start collecting data */
                    FURI_LOG_D(TAG, "Preamble: %luus", instance->preamble_len);
                    instance->decoder.parser_step = OpelV0DecoderStepData;
                    instance->bit_count = 0;
                }
                instance->current_burst = 0;
            } else {
                /* Very large gap: reset */
                instance->decoder.parser_step = OpelV0DecoderStepReset;
            }
        }
        break;

    case OpelV0DecoderStepData:
        if(level) {
            /* Mark: accumulate into current burst */
            instance->current_burst += duration;

            /* If burst is getting very large, it's a new preamble */
            if(instance->current_burst > 10000) {
                /* End current frame */
                subghz_protocol_opel_v0_process_frame(instance);
                /* Start new preamble */
                instance->preamble_len = instance->current_burst;
                instance->current_burst = 0;
                instance->bit_count = 0;
                instance->decoder.parser_step = OpelV0DecoderStepPreamble;
            }
        } else {
            /* Space */
            if(duration < OPEL_V0_MERGE_THRESHOLD) {
                /* Tiny space: within carrier, merge */
                instance->current_burst += duration;
            } else if(duration >= OPEL_V0_GAP_MIN && duration <= OPEL_V0_GAP_MAX) {
                /* Normal data gap: end of one burst = one bit */
                if(instance->current_burst > 500 &&
                   instance->bit_count < OPEL_V0_MAX_BITS) {
                    instance->burst_widths[instance->bit_count] =
                        (instance->current_burst > 0xFFFF) ? 0xFFFF : instance->current_burst;
                    instance->bit_count++;
                }
                instance->current_burst = 0;
            } else if(duration > OPEL_V0_FRAME_GAP) {
                /* Frame end gap */
                if(instance->current_burst > 500 &&
                   instance->bit_count < OPEL_V0_MAX_BITS) {
                    instance->burst_widths[instance->bit_count] =
                        (instance->current_burst > 0xFFFF) ? 0xFFFF : instance->current_burst;
                    instance->bit_count++;
                }
                subghz_protocol_opel_v0_process_frame(instance);
                instance->current_burst = 0;
                instance->bit_count = 0;
                instance->preamble_len = 0;
                instance->decoder.parser_step = OpelV0DecoderStepReset;
            } else {
                /* Gap between preamble range and data range: could be noise */
                /* Treat as within-burst if burst is accumulating */
                if(instance->current_burst > 0) {
                    instance->current_burst += duration;
                }
            }
        }
        break;
    }
}

// ============================================================================
// Decoder: hash / serialize / deserialize
// ============================================================================





SubGhzProtocolStatus
    subghz_protocol_decoder_opel_v0_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderOpelV0* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_opel_v0_const.min_count_bit_for_found);
}

// ============================================================================
// Decoder: get_string
// ============================================================================

