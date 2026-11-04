/**
 * @file fiat_v2_v3_v5.c
 * @brief Combined Fiat V3/V5 SubGHz Protocol — single "Fiat" protocol
 *
 * One protocol registration, variant detected at runtime from signal timing.
 * V3: 70-bit frame (preamble + 22 fix + 2 btn + 2 status + 44 hop), te_short=460us
 * V5: 97-bit frame (preamble + 32 fix + 8 type + 8 btn + 8 seed + 8 cnt + 32 hop + 1 trail), te_short=200us
 */

#include "fiat_v2_v3_v5.h"

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#include <lib/subghz/blocks/custom_btn_i.h>
#include "common.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"

/* FoxFW: positional custom-button helpers. The applab source used map-less "_positional"
 * variants that FoxFW's custom_btn lib does not provide; defined file-local so nothing is
 * added to the core lib. Positional mode: btns[] is the selectable set, custom_id (1-based)
 * picks btns[custom_id-1] (custom_max == count); custom_id 0 / out-of-range falls back to the
 * decoded button. fill_labels is a no-op, matching lib/subghz/blocks/custom_btn.c. */
#define TAG "SubGhzProtocolFiat"

// ============================================================================
// TIMING CONSTANTS
// ============================================================================

// V3
#define FIAT_V3_TE_SHORT         460
#define FIAT_V3_TE_LONG          920
#define FIAT_V3_TE_DELTA         200
#define FIAT_V3_MIN_GAP          1500
#define FIAT_V3_FRAME_BITS       70
#define FIAT_V3_MIN_FRAME_BITS   60
#define FIAT_V3_MAX_BITS         100
#define FIAT_V3_PREAMBLE_MIN     4
#define FIAT_V3_PREAMBLE_MAX     20
#define FIAT_V3_PREAMBLE_BITS    13
#define FIAT_V3_MAX_TIMINGS      170
#define FIAT_V3_MIN_PREAMBLE_GAP 400

static const SubGhzBlockConst subghz_protocol_fiat_v3_const = {
    .te_short = FIAT_V3_TE_SHORT,
    .te_long = FIAT_V3_TE_LONG,
    .te_delta = FIAT_V3_TE_DELTA,
    .min_count_bit_for_found = FIAT_V3_MIN_FRAME_BITS,
};

// V5
#define FIAT_V5_TE_SHORT       200
#define FIAT_V5_TE_LONG        400
#define FIAT_V5_TE_DELTA       100
#define FIAT_V5_MIN_GAP        2000
#define FIAT_V5_FRAME_BITS     97
#define FIAT_V5_MIN_FRAME_BITS 90
#define FIAT_V5_MAX_BITS       130
#define FIAT_V5_PREAMBLE_MIN   10
#define FIAT_V5_PREAMBLE_BITS  15

static const SubGhzBlockConst subghz_protocol_fiat_v5_const = {
    .te_short = FIAT_V5_TE_SHORT,
    .te_long = FIAT_V5_TE_LONG,
    .te_delta = FIAT_V5_TE_DELTA,
    .min_count_bit_for_found = FIAT_V5_MIN_FRAME_BITS,
};

// ============================================================================
// MANCHESTER TABLE & SHARED DECODE
// ============================================================================

static const uint8_t fiat_manchester_table[4] = {0x01, 0x91, 0x9B, 0xFB};

static bool fiat_manchester_decode(uint8_t* state, uint8_t timing_type, uint8_t* bit_out) {
    if(timing_type == 8) {
        *state = 1;
        return false;
    }
    if(*state >= 4) {
        *state = 1;
        return false;
    }

    uint8_t entry = fiat_manchester_table[*state];
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
    FiatVariantV3 = 3,
    FiatVariantV5 = 5,
} FiatVariant;

// Index for the params table (0,1)
#define FIAT_VARIANT_IDX(v) ((v) == FiatVariantV3 ? 0 : 1)
#define FIAT_VARIANT_COUNT  2

typedef struct {
    FiatVariant variant;
    const SubGhzBlockConst* block_const;
    uint32_t min_gap;
    uint16_t frame_bits;
    uint16_t max_bits;
    uint8_t preamble_bits;
    uint8_t btn_max;
    bool has_counter;
    uint8_t cnt_length_bit;
    uint8_t btn_length_bit;
    const char* display_name;
} FiatVariantParams;

static const FiatVariantParams fiat_variant_params[FIAT_VARIANT_COUNT] = {
    {
        .variant = FiatVariantV3,
        .block_const = &subghz_protocol_fiat_v3_const,
        .min_gap = FIAT_V3_MIN_GAP,
        .frame_bits = FIAT_V3_FRAME_BITS,
        .max_bits = FIAT_V3_MAX_BITS,
        .preamble_bits = FIAT_V3_PREAMBLE_BITS,
        .btn_max = 3,
        .has_counter = false,
        .cnt_length_bit = 0,
        .btn_length_bit = 2,
        .display_name = "Fiat V3",
    },
    {
        .variant = FiatVariantV5,
        .block_const = &subghz_protocol_fiat_v5_const,
        .min_gap = FIAT_V5_MIN_GAP,
        .frame_bits = FIAT_V5_FRAME_BITS,
        .max_bits = FIAT_V5_MAX_BITS,
        .preamble_bits = FIAT_V5_PREAMBLE_BITS,
        .btn_max = 3,
        .has_counter = true,
        .cnt_length_bit = 8,
        .btn_length_bit = 8,
        .display_name = "Fiat V5",
    },
};

static const FiatVariantParams* fiat_get_params(FiatVariant v) {
    return &fiat_variant_params[FIAT_VARIANT_IDX(v)];
}

// ============================================================================
// BUTTON CONSTANTS
// ============================================================================

// V3 buttons
#define FIAT_V3_BTN_LOCK   0x00
#define FIAT_V3_BTN_UNLOCK 0x01
#define FIAT_V3_BTN_TRUNK  0x02

// V5 buttons
#define FIAT_V5_BTN_OPEN  0x80
#define FIAT_V5_BTN_TRUNK 0x40
#define FIAT_V5_BTN_CLOSE 0x20

// ============================================================================
// UNIFIED BUTTON FUNCTIONS
// ============================================================================


static bool fiat_v5_is_valid_button(uint8_t btn) {
    return (btn == FIAT_V5_BTN_OPEN || btn == FIAT_V5_BTN_TRUNK || btn == FIAT_V5_BTN_CLOSE);
}

// ============================================================================
// DATA STRUCTURES
// ============================================================================

typedef enum {
    FiatDecoderStepReset = 0,
    FiatDecoderStepData = 1,
} FiatDecoderStep;

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    FiatVariant variant;
    uint8_t manchester_state;
    uint8_t bits[150];
    uint16_t bit_count;

    union {
        struct {
            uint32_t fix_value;
            uint8_t btn;
            uint8_t status_bits;
            uint64_t hop_value;
            uint8_t hop_low;
            uint16_t timing_count;
            uint32_t preamble_gap_sum;
            uint16_t preamble_gap_count;
            bool preamble_ended;
        } v3;
        struct {
            uint32_t fix_value;
            uint8_t type_byte;
            uint8_t btn;
            uint8_t seed;
            uint8_t cnt;
            uint32_t hop_value;
        } v5;
    } extra;
} SubGhzProtocolDecoderFiat;

typedef struct {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    FiatVariant variant;

    union {
        struct {
            uint32_t fix_value;
            uint8_t btn;
            uint8_t status_bits;
            uint64_t hop_value;
            uint8_t hop_low;
        } v3;
        struct {
            uint32_t fix_value;
            uint8_t type_byte;
            uint8_t btn;
            uint8_t seed;
            uint8_t cnt;
            uint32_t hop_value;
        } v5;
    } extra;
} SubGhzProtocolEncoderFiat;

// ============================================================================
// PROTOCOL DEFINITION — single VTable, single registration
// ============================================================================


const SubGhzProtocolDecoder subghz_protocol_fiat_decoder = {
    .alloc = subghz_protocol_decoder_fiat_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_fiat_feed,
    .reset = subghz_protocol_decoder_fiat_reset,
    .get_hash_data = subghz_protocol_decoder_fiat_get_hash_data,
    .serialize = subghz_protocol_decoder_fiat_serialize,
    .deserialize = subghz_protocol_decoder_fiat_deserialize,
    .get_string = subghz_protocol_decoder_fiat_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_fiat_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_fiat_v2_v3_v5");

const SubGhzProtocol subghz_protocol_fiat_v2_v3_v5 = {
    .name = SUBGHZ_PROTOCOL_FIAT_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_fiat_decoder,
    .encoder = &subghz_protocol_fiat_encoder_ext.vtable,
};

// ============================================================================
// V3 HOP LINEAR CRYPTANALYSIS
// ============================================================================

/**
 * Extract button value from V3 hop code using linear cryptanalysis.
 *   btn = XOR of bits {4,5,12,13,20,21,28,29,36,37}
 *
 * Returns 0 for LOCK, 1 for UNLOCK
 */
static uint8_t fiat_v3_hop_btn_extract(uint64_t hop) {
    /* Mask selects bit-pairs (4,5) in each of the 5 full bytes */
    const uint64_t mask = (3ULL << 4) | (3ULL << 12) | (3ULL << 20) | (3ULL << 28) | (3ULL << 36);
    uint64_t masked = hop & mask;

    /* popcount parity: XOR of all selected bits = popcount mod 2 */
    masked ^= masked >> 32;
    masked ^= masked >> 16;
    masked ^= masked >> 8;
    masked ^= masked >> 4;
    masked ^= masked >> 2;
    masked ^= masked >> 1;
    return (uint8_t)(masked & 1);
}

/**
 * Validate V3 hop against the 5 known cipher invariants.
 */
static bool fiat_v3_hop_invariants_check(uint64_t hop) {
    /* R1: XOR bits {5,6,13,14,21,22,29,30,37,38} = 0 */
    const uint64_t r1 = (3ULL << 5) | (3ULL << 13) | (3ULL << 21) | (3ULL << 29) | (3ULL << 37);
    /* R2: XOR bits {5,7,13,15,21,23,29,31,37,39} = 0 */
    const uint64_t r2 = (5ULL << 5) | (5ULL << 13) | (5ULL << 21) | (5ULL << 29) | (5ULL << 37);
    /* R3: XOR bits {5,8,13,16,21,24,29,32,37,40} = 0 */
    const uint64_t r3 = (1ULL << 5) | (1ULL << 8) | (1ULL << 13) | (1ULL << 16) |
                         (1ULL << 21) | (1ULL << 24) | (1ULL << 29) | (1ULL << 32) |
                         (1ULL << 37) | (1ULL << 40);
    /* R4: XOR bits {1,9,17,25,33,41} = 0 */
    const uint64_t r4 = (1ULL << 1) | (1ULL << 9) | (1ULL << 17) | (1ULL << 25) |
                         (1ULL << 33) | (1ULL << 41);

    const uint64_t masks[] = {r1, r2, r3, r4};
    for(int i = 0; i < 4; i++) {
        uint64_t v = hop & masks[i];
        v ^= v >> 32;
        v ^= v >> 16;
        v ^= v >> 8;
        v ^= v >> 4;
        v ^= v >> 2;
        v ^= v >> 1;
        if(v & 1) return false;
    }
    return true;
}

// ============================================================================
// V3 PACK / UNPACK helpers
// ============================================================================

static void fiat_v3_pack_generic(
    SubGhzBlockGeneric* generic,
    uint32_t fix,
    uint8_t btn,
    uint64_t hop,
    uint8_t* hop_low_out) {
    generic->data = ((uint64_t)(fix & 0x3FFFFF) << 42) | ((uint64_t)(btn & 0x3) << 40) |
                    ((hop >> 4) & 0xFFFFFFFFFFULL);
    generic->data_count_bit = FIAT_V3_FRAME_BITS;
    generic->serial = fix;
    generic->btn = btn;
    generic->cnt = 0;
    if(hop_low_out) *hop_low_out = hop & 0xF;
}

static void fiat_v3_unpack_generic(
    const SubGhzBlockGeneric* generic,
    uint8_t hop_low,
    uint32_t* fix_out,
    uint8_t* btn_out,
    uint64_t* hop_out) {
    uint32_t fix = (generic->data >> 42) & 0x3FFFFF;
    uint8_t btn = (generic->data >> 40) & 0x3;
    uint64_t hop = ((generic->data & 0xFFFFFFFFFFULL) << 4) | (hop_low & 0xF);
    if(fix_out) *fix_out = fix;
    if(btn_out) *btn_out = btn;
    if(hop_out) *hop_out = hop;
}

// ============================================================================
// V5 PACK / UNPACK helpers
// ============================================================================

static void fiat_v5_pack_generic(
    SubGhzBlockGeneric* generic,
    uint32_t fix,
    uint8_t btn,
    uint8_t cnt,
    uint32_t hop) {
    generic->data = ((uint64_t)fix << 32) | ((uint64_t)hop & 0xFFFFFFFF);
    generic->data_count_bit = FIAT_V5_FRAME_BITS;
    generic->serial = fix;
    generic->btn = btn;
    generic->cnt = cnt;
}

static void fiat_v5_unpack_generic(
    const SubGhzBlockGeneric* generic,
    uint32_t* fix_out,
    uint32_t* hop_out) {
    if(fix_out) *fix_out = (uint32_t)(generic->data >> 32);
    if(hop_out) *hop_out = (uint32_t)(generic->data & 0xFFFFFFFF);
}

// ============================================================================
// UNIFIED FRAME PROCESSING
// ============================================================================

static void fiat_process_frame(SubGhzProtocolDecoderFiat* instance) {
    uint16_t start = 0;
    while(start < instance->bit_count && instance->bits[start] == 1) {
        start++;
    }
    uint16_t preamble_len = start;
    uint16_t data_len = instance->bit_count - start;

    switch(instance->variant) {
    case FiatVariantV3: {
        if(preamble_len < FIAT_V3_PREAMBLE_MIN || data_len < FIAT_V3_MIN_FRAME_BITS) return;
        if(preamble_len > FIAT_V3_PREAMBLE_MAX) return;
        if(instance->bit_count >= FIAT_V3_MAX_BITS) return;
        if(instance->extra.v3.timing_count > FIAT_V3_MAX_TIMINGS) return;

        if(instance->extra.v3.preamble_gap_count > 0) {
            uint32_t avg_gap =
                instance->extra.v3.preamble_gap_sum / instance->extra.v3.preamble_gap_count;
            if(avg_gap < FIAT_V3_MIN_PREAMBLE_GAP) return;
        }


        if(data_len > FIAT_V3_FRAME_BITS) data_len = FIAT_V3_FRAME_BITS;

        uint32_t fix = 0;
        for(uint16_t i = 0; i < 22 && i < data_len; i++) {
            fix = (fix << 1) | instance->bits[start + i];
        }
        uint8_t btn = 0;
        for(uint16_t i = 22; i < 24 && i < data_len; i++) {
            btn = (btn << 1) | instance->bits[start + i];
        }
        uint8_t status = 0;
        for(uint16_t i = 24; i < 26 && i < data_len; i++) {
            status = (status << 1) | instance->bits[start + i];
        }
        uint64_t hop = 0;
        for(uint16_t i = 26; i < 70 && i < data_len; i++) {
            hop = (hop << 1) | instance->bits[start + i];
        }

        // Structural validation on complete frames:
        // hop[43:42] must be 00, hop[0] must be 1 (stop bit)
        if(data_len >= FIAT_V3_FRAME_BITS) {
            if((hop >> 42) != 0) return;
            if((hop & 1) != 1) return;
        }

        instance->extra.v3.fix_value = fix;
        instance->extra.v3.btn = btn;
        instance->extra.v3.status_bits = status;
        instance->extra.v3.hop_value = hop;
        fiat_v3_pack_generic(&instance->generic, fix, btn, hop, &instance->extra.v3.hop_low);

        // Validate hop via linear cryptanalysis:
        // button extracted from hop must match frame button field
        uint8_t hop_btn = fiat_v3_hop_btn_extract(hop);
        if(hop_btn != btn) return;
        /* Require the hop invariants to hold before emitting, and only then
         * mark crc_check — previously a frame with failing invariants still
         * surfaced as a decode with crc_check=ERR. */
        if(!fiat_v3_hop_invariants_check(hop)) return;
        instance->generic.extra_check = true;
        instance->generic.crc_check = true;
    } break;

    case FiatVariantV5: {
        if(preamble_len < FIAT_V5_PREAMBLE_MIN || data_len < FIAT_V5_MIN_FRAME_BITS) return;
        if(data_len > FIAT_V5_FRAME_BITS) data_len = FIAT_V5_FRAME_BITS;

        uint32_t fix = 0;
        for(uint16_t i = 0; i < 32 && i < data_len; i++) {
            fix = (fix << 1) | instance->bits[start + i];
        }
        uint8_t type_byte = 0;
        for(uint16_t i = 32; i < 40 && i < data_len; i++) {
            type_byte = (type_byte << 1) | instance->bits[start + i];
        }
        uint8_t btn = 0;
        for(uint16_t i = 40; i < 48 && i < data_len; i++) {
            btn = (btn << 1) | instance->bits[start + i];
        }
        uint8_t seed = 0;
        for(uint16_t i = 48; i < 56 && i < data_len; i++) {
            seed = (seed << 1) | instance->bits[start + i];
        }
        uint8_t cnt = 0;
        for(uint16_t i = 56; i < 64 && i < data_len; i++) {
            cnt = (cnt << 1) | instance->bits[start + i];
        }
        uint32_t hop = 0;
        for(uint16_t i = 64; i < 96 && i < data_len; i++) {
            hop = (hop << 1) | instance->bits[start + i];
        }

        if(!fiat_v5_is_valid_button(btn)) return;

        // Structural validation on complete frames: hop[0] must be 1 (stop bit)
        if(data_len >= FIAT_V5_FRAME_BITS) {
            if((hop & 1) != 1) return;
        }

        instance->extra.v5.fix_value = fix;
        instance->extra.v5.type_byte = type_byte;
        instance->extra.v5.btn = btn;
        instance->extra.v5.seed = seed;
        instance->extra.v5.cnt = cnt;
        instance->extra.v5.hop_value = hop;
        fiat_v5_pack_generic(&instance->generic, fix, btn, cnt, hop);
    } break;

    default:
        return;
    }

    // Set decode_data so get_hash_data returns a meaningful hash for deduplication
    instance->decoder.decode_data = instance->generic.data;

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

// ============================================================================
// SHARED DECODE TIMING
// ============================================================================

static void
    fiat_decode_timing(SubGhzProtocolDecoderFiat* instance, bool level, uint32_t duration) {
    const FiatVariantParams* params = fiat_get_params(instance->variant);
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
        // V3 preamble validation & timing quality tracking
        if(instance->variant == FiatVariantV3) {
            instance->extra.v3.timing_count++;
            if(!level && !instance->extra.v3.preamble_ended) {
                instance->extra.v3.preamble_gap_sum += duration;
                instance->extra.v3.preamble_gap_count++;
            }
            if(instance->extra.v3.timing_count > FIAT_V3_MAX_TIMINGS) {
                instance->decoder.parser_step = FiatDecoderStepReset;
                instance->bit_count = 0;
                instance->manchester_state = 1;
                instance->extra.v3.timing_count = 0;
                instance->extra.v3.preamble_gap_sum = 0;
                instance->extra.v3.preamble_gap_count = 0;
                instance->extra.v3.preamble_ended = false;
                return;
            }
        }

        uint8_t bit_value = 0;
        if(fiat_manchester_decode(&instance->manchester_state, timing_type, &bit_value)) {
            if(instance->bit_count < params->max_bits) {
                instance->bits[instance->bit_count++] = bit_value;
            }

            if(instance->variant == FiatVariantV3 && bit_value == 0 &&
               !instance->extra.v3.preamble_ended) {
                instance->extra.v3.preamble_ended = true;
            }

            if(instance->bit_count >= params->max_bits) {
                fiat_process_frame(instance);
                instance->decoder.parser_step = FiatDecoderStepReset;
                instance->bit_count = 0;
                instance->manchester_state = 1;
                if(instance->variant == FiatVariantV3) {
                    instance->extra.v3.timing_count = 0;
                    instance->extra.v3.preamble_gap_sum = 0;
                    instance->extra.v3.preamble_gap_count = 0;
                    instance->extra.v3.preamble_ended = false;
                }
            }
        }
    } else {
        if(instance->bit_count >= bc->min_count_bit_for_found) {
            fiat_process_frame(instance);
        }
        instance->decoder.parser_step = FiatDecoderStepReset;
        instance->bit_count = 0;
        instance->manchester_state = 1;
        if(instance->variant == FiatVariantV3) {
            instance->extra.v3.timing_count = 0;
            instance->extra.v3.preamble_gap_sum = 0;
            instance->extra.v3.preamble_gap_count = 0;
            instance->extra.v3.preamble_ended = false;
        }
    }
}

// ============================================================================
// DECODER — ALLOC / FREE / RESET
// ============================================================================

void* subghz_protocol_decoder_fiat_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);

    SubGhzProtocolDecoderFiat* instance = malloc(sizeof(SubGhzProtocolDecoderFiat));
    instance->base.protocol = &subghz_protocol_fiat_v2_v3_v5;
    instance->generic.protocol_name = subghz_protocol_fiat_v2_v3_v5.name;
    instance->variant = FiatVariantV3; // default, detected at runtime
    instance->manchester_state = 1;
    instance->bit_count = 0;
    memset(instance->bits, 0, sizeof(instance->bits));
    memset(&instance->extra, 0, sizeof(instance->extra));

    return instance;
}



void subghz_protocol_decoder_fiat_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderFiat* instance = context;

    instance->decoder.parser_step = FiatDecoderStepReset;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->manchester_state = 1;
    instance->bit_count = 0;
    memset(&instance->extra, 0, sizeof(instance->extra));
}

// ============================================================================
// DECODER — FEED (variant auto-detection from te_short timing)
// ============================================================================

void subghz_protocol_decoder_fiat_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderFiat* instance = context;

    switch(instance->decoder.parser_step) {
    case FiatDecoderStepReset: {
        // Detect variant from timing — best-fit te_short match
        // Manchester encoder always starts with te_short, so this
        // cleanly separates V2(100), V5(200), V3(460)
        int best_idx = -1;
        uint32_t best_diff = UINT32_MAX;

        for(int i = 0; i < FIAT_VARIANT_COUNT; i++) {
            const SubGhzBlockConst* bc = fiat_variant_params[i].block_const;
            uint32_t diff = DURATION_DIFF(duration, bc->te_short);
            if(diff <= bc->te_delta && diff < best_diff) {
                best_diff = diff;
                best_idx = i;
            }
        }

        if(best_idx >= 0) {
            instance->variant = fiat_variant_params[best_idx].variant;
            instance->decoder.parser_step = FiatDecoderStepData;
            instance->manchester_state = 1;
            instance->bit_count = 0;
            memset(&instance->extra, 0, sizeof(instance->extra));
            fiat_decode_timing(instance, level, duration);
        }
    } break;

    case FiatDecoderStepData: {
        const FiatVariantParams* params = fiat_get_params(instance->variant);

        // Frame boundary: gap > min_gap
        if(duration > params->min_gap) {
            if(instance->bit_count >= params->block_const->min_count_bit_for_found) {
                fiat_process_frame(instance);
            }
            instance->decoder.parser_step = FiatDecoderStepReset;
            instance->bit_count = 0;
            instance->manchester_state = 1;
            memset(&instance->extra, 0, sizeof(instance->extra));
            return;
        }

        fiat_decode_timing(instance, level, duration);
    } break;

    default:
        instance->decoder.parser_step = FiatDecoderStepReset;
        break;
    }
}

// ============================================================================
// DECODER — GET HASH DATA
// ============================================================================

uint8_t subghz_protocol_decoder_fiat_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderFiat* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, sizeof(uint64_t));
}

// ============================================================================
// DECODER — SERIALIZE
// ============================================================================

SubGhzProtocolStatus subghz_protocol_decoder_fiat_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderFiat* dec = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&dec->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    // Write variant number
    uint32_t variant_temp = (uint32_t)dec->variant;
    if(!flipper_format_insert_or_update_uint32(flipper_format, "Variant", &variant_temp, 1)) {
        return SubGhzProtocolStatusError;
    }

    switch(dec->variant) {
    case FiatVariantV3: {
        uint32_t hop_low_temp = dec->extra.v3.hop_low;
        if(!flipper_format_insert_or_update_uint32(flipper_format, "HopLow", &hop_low_temp, 1)) {
            return SubGhzProtocolStatusError;
        }
        uint32_t status_temp = dec->extra.v3.status_bits;
        if(!flipper_format_insert_or_update_uint32(flipper_format, "Status", &status_temp, 1)) {
            return SubGhzProtocolStatusError;
        }
    } break;

    case FiatVariantV5: {
        uint32_t type_temp = dec->extra.v5.type_byte;
        if(!flipper_format_insert_or_update_uint32(flipper_format, "Type", &type_temp, 1)) {
            return SubGhzProtocolStatusError;
        }
        uint32_t seed_temp = dec->extra.v5.seed;
        if(!flipper_format_insert_or_update_uint32(flipper_format, "Seed", &seed_temp, 1)) {
            return SubGhzProtocolStatusError;
        }
        uint32_t btn_temp = dec->extra.v5.btn;
        if(!flipper_format_insert_or_update_uint32(flipper_format, "Btn", &btn_temp, 1)) {
            return SubGhzProtocolStatusError;
        }
        uint32_t cnt_temp = dec->extra.v5.cnt;
        if(!flipper_format_insert_or_update_uint32(flipper_format, "Cnt", &cnt_temp, 1)) {
            return SubGhzProtocolStatusError;
        }
    } break;
    }

    return SubGhzProtocolStatusOk;
}

// ============================================================================
// DECODER — DESERIALIZE
// ============================================================================

SubGhzProtocolStatus
    subghz_protocol_decoder_fiat_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderFiat* dec = context;

    // Read generic data first (Key, Bit)
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize(&dec->generic, flipper_format);
    if(ret != SubGhzProtocolStatusOk) return ret;

    // Read variant — or auto-detect from Bit count
    uint32_t variant_temp = 0;
    if(flipper_format_read_uint32(flipper_format, "Variant", &variant_temp, 1)) {
        dec->variant = (FiatVariant)variant_temp;
    } else {
        switch(dec->generic.data_count_bit) {
        case FIAT_V3_FRAME_BITS:
            dec->variant = FiatVariantV3;
            break;
        case FIAT_V5_FRAME_BITS:
            dec->variant = FiatVariantV5;
            break;
        default:
            return SubGhzProtocolStatusErrorValueBitCount;
        }
    }

    const FiatVariantParams* params = fiat_get_params(dec->variant);

    // Validate bit count
    if(dec->generic.data_count_bit != params->frame_bits) {
        return SubGhzProtocolStatusErrorValueBitCount;
    }

    switch(dec->variant) {
    case FiatVariantV3: {
        uint32_t hop_low_temp = 0;
        flipper_format_read_uint32(flipper_format, "HopLow", &hop_low_temp, 1);
        dec->extra.v3.hop_low = hop_low_temp & 0xF;

        uint32_t status_temp = 0;
        flipper_format_read_uint32(flipper_format, "Status", &status_temp, 1);
        dec->extra.v3.status_bits = status_temp & 0x3;

        uint32_t fix;
        uint8_t btn;
        uint64_t hop;
        fiat_v3_unpack_generic(&dec->generic, dec->extra.v3.hop_low, &fix, &btn, &hop);
        dec->extra.v3.fix_value = fix;
        dec->extra.v3.btn = btn;
        dec->extra.v3.hop_value = hop;

        dec->generic.serial = fix;
        dec->generic.btn = btn;
    } break;

    case FiatVariantV5: {
        uint32_t type_temp = 0;
        flipper_format_read_uint32(flipper_format, "Type", &type_temp, 1);
        dec->extra.v5.type_byte = type_temp & 0xFF;

        uint32_t seed_temp = 0;
        flipper_format_read_uint32(flipper_format, "Seed", &seed_temp, 1);
        dec->extra.v5.seed = seed_temp & 0xFF;

        uint32_t btn_temp = 0;
        if(flipper_format_read_uint32(flipper_format, "Btn", &btn_temp, 1)) {
            dec->extra.v5.btn = btn_temp & 0xFF;
            dec->generic.btn = dec->extra.v5.btn;
        }

        uint32_t cnt_temp = 0;
        if(flipper_format_read_uint32(flipper_format, "Cnt", &cnt_temp, 1)) {
            dec->extra.v5.cnt = cnt_temp & 0xFF;
            dec->generic.cnt = dec->extra.v5.cnt;
        }

        fiat_v5_unpack_generic(&dec->generic, &dec->extra.v5.fix_value, &dec->extra.v5.hop_value);
        dec->generic.serial = dec->extra.v5.fix_value;
    } break;
    }

    subghz_custom_btn_set_original_if_unset(dec->generic.btn);
    subghz_custom_btn_set_max(params->btn_max);

    return SubGhzProtocolStatusOk;
}

// ============================================================================
// DECODER — GET STRING
// ============================================================================

void subghz_protocol_decoder_fiat_get_string(void* context, FuriString* output) {
    SubGhzProtocolDecoderFiat* dec = context;
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

// ============================================================================
// UNIFIED BUILD UPLOAD
// ============================================================================

