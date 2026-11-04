/**
 * Peugot V0 SubGHz Protocol - 80-bit encrypted rolling code (PSA)
 *
 * 88-bit frame: [Sync:8=FE][Data:80]
 * - Sync: FE (11111110) — end-of-preamble marker (last bit = 0)
 * - Data: 80-bit, starts with FFAD header, fully encrypted rolling code
 *   - Bits 0-63: stored in generic.data (Key)
 *   - Bits 64-79: stored in data_16 (Key2)
 *
 * Physical layer: Manchester, ~220/440us, long preamble (80+ bits of 1),
 * FE byte marks end of preamble, followed by 80 data bits.
 *
 * 433.92 MHz, OOK
 *
 * Decoder uses stateless Manchester (Ansatz C):
 * - Collect all bits (preamble + sync + data)
 * - Search for FEFFAD pattern for robust sync detection
 * - Extract 80 data bits starting after FE (includes FFAD + 64 bit payload)
 */

#include "peugot_v0.h"

#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#include <furi.h>
#include <furi_hal.h>
#include "common.h"
#include "../encode_plugin.h"
#include <lib/subghz/blocks/bitbuf.h>

#define TAG "SubGhzProtocolPeugotV0"

#define PEUGOT_V0_DATA_BITS         80
#define PEUGOT_V0_MAX_BITS          256
#define PEUGOT_V0_MIN_PREAMBLE_BITS 30
#define PEUGOT_V0_SYNC_LEN          8      // FE = 11111110 (preamble end marker)
#define PEUGOT_V0_MIN_DATA_BITS     70

// Sync pattern: FE = 11111110 (end-of-preamble marker)
// We search for the full FEFFAD (24 bits) for robustness, but data starts after FE (8 bits)
static const uint8_t peugot_v0_sync_pattern_full[] = {
    1,1,1,1,1,1,1,0,  // 0xFE
    1,1,1,1,1,1,1,1,  // 0xFF
    1,0,1,0,1,1,0,1   // 0xAD
};
#define PEUGOT_V0_SYNC_SEARCH_LEN 24

static const SubGhzBlockConst subghz_protocol_peugot_v0_const = {
    .te_short = 220,
    .te_long = 440,
    .te_delta = 100,
    .min_count_bit_for_found = PEUGOT_V0_DATA_BITS,
};

// ============================================================================
// DATA STRUCTURES
// ============================================================================

struct SubGhzProtocolDecoderPeugotV0 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t all_bits[SUBGHZ_BITBUF_BYTES(PEUGOT_V0_MAX_BITS)];
    uint16_t bit_count;
    bool half_bit;

    uint16_t data_16;  // Bits 64-79 of the 80-bit frame
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderPeugotV0);


struct SubGhzProtocolEncoderPeugotV0 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint16_t data_16;  // Bits 64-79 for encoding
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderPeugotV0);


// ============================================================================
// PROTOCOL DEFINITION
// ============================================================================


const SubGhzProtocolDecoder subghz_protocol_peugot_v0_decoder = {
    .alloc = subghz_protocol_decoder_peugot_v0_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_peugot_v0_feed,
    .reset = subghz_protocol_decoder_peugot_v0_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_peugot_v0_serialize,
    .deserialize = subghz_protocol_decoder_peugot_v0_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_peugot_v0_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_peugot_v0");

const SubGhzProtocol subghz_protocol_peugot_v0 = {
    .name = SUBGHZ_PROTOCOL_PEUGOT_V0_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_peugot_v0_decoder,
    .encoder = &subghz_protocol_peugot_v0_encoder_ext.vtable,
};

// ============================================================================
// DECODER IMPLEMENTATION
// ============================================================================

void* subghz_protocol_decoder_peugot_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderPeugotV0* instance = malloc(sizeof(SubGhzProtocolDecoderPeugotV0));
    instance->base.protocol = &subghz_protocol_peugot_v0;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}



void subghz_protocol_decoder_peugot_v0_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderPeugotV0* instance = context;
    instance->decoder.parser_step = 0;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->bit_count = 0;
    instance->half_bit = false;
    instance->data_16 = 0;
}

/**
 * Search for sync pattern and extract 80 data bits from collected bits.
 */
static void peugot_v0_process_frame(SubGhzProtocolDecoderPeugotV0* instance) {
    if(instance->bit_count < (PEUGOT_V0_SYNC_LEN + PEUGOT_V0_MIN_DATA_BITS)) return;

    // Anchor the FE FF AD marker to the end of a >=30-bit preamble run instead
    // of a free scan of the whole 256-bit buffer. FE = 1111_1110, so the run of
    // preamble 1-bits flows into the marker's 7 leading ones and terminates at
    // FE's low 0 bit; that transition is the only place a genuine marker starts.
    // Rejects coincidental FEFFAD patterns buried in noise/payload.
    int sync_pos = -1;
    int run = 0;
    for(int i = 0; i + PEUGOT_V0_SYNC_SEARCH_LEN <= (int)instance->bit_count; i++) {
        if(subghz_bitbuf_get(instance->all_bits, i) == 1) {
            run++;
            continue;
        }
        // 'i' is the first 0 after a run of 1s. Require >=30 preamble ones plus
        // FE's 7 leading ones, then match FE FF AD starting 7 bits back.
        if(run >= (PEUGOT_V0_MIN_PREAMBLE_BITS + 7) && i >= 7) {
            int cand = i - 7;  // start of FE
            bool match = true;
            for(int j = 0; j < PEUGOT_V0_SYNC_SEARCH_LEN; j++) {
                if(subghz_bitbuf_get(instance->all_bits, cand + j) != peugot_v0_sync_pattern_full[j]) {
                    match = false;
                    break;
                }
            }
            if(match) {
                sync_pos = cand;
                break;
            }
        }
        run = 0;
    }

    if(sync_pos < 0) {
        FURI_LOG_D(TAG, "No preamble-anchored sync in %u bits", instance->bit_count);
        return;
    }

    int preamble_ones = run - 7;  // ones counted before the FE marker

    // Require exactly the full 80-bit data field after FE (FFAD + 64b payload).
    int data_start = sync_pos + PEUGOT_V0_SYNC_LEN;  // +8 = after FE
    int data_available = instance->bit_count - data_start;
    if(data_available < PEUGOT_V0_DATA_BITS) {
        FURI_LOG_D(TAG, "Data too short: %d bits (need %d)", data_available, PEUGOT_V0_DATA_BITS);
        return;
    }
    int data_bits = PEUGOT_V0_DATA_BITS;  // exactly 80

    // Extract first 64 bits → generic.data
    uint64_t data = 0;
    int bits_for_data = (data_bits > 64) ? 64 : data_bits;
    for(int i = 0; i < bits_for_data; i++) {
        data = (data << 1) | subghz_bitbuf_get(instance->all_bits, data_start + i);
    }
    if(bits_for_data < 64) {
        data <<= (64 - bits_for_data);
    }

    // Extract bits 64-79 → data_16
    uint16_t data_16 = 0;
    if(data_bits > 64) {
        int extra_bits = data_bits - 64;
        if(extra_bits > 16) extra_bits = 16;
        for(int i = 0; i < extra_bits; i++) {
            data_16 = (data_16 << 1) | subghz_bitbuf_get(instance->all_bits, data_start + 64 + i);
        }
        if(extra_bits < 16) {
            data_16 <<= (16 - extra_bits);
        }
    }

    // Reject degenerate all-zero / all-one payloads (observed false positive).
    if((data == 0 && data_16 == 0) ||
       (data == 0xFFFFFFFFFFFFFFFFULL && data_16 == 0xFFFF)) {
        FURI_LOG_D(TAG, "Degenerate payload rejected");
        return;
    }

    instance->generic.data = data;
    instance->generic.data_count_bit = PEUGOT_V0_DATA_BITS;
    instance->data_16 = data_16;

    uint32_t key_hi = (uint32_t)(data >> 32);
    uint32_t key_lo = (uint32_t)(data & 0xFFFFFFFF);

    FURI_LOG_I(
        TAG,
        "Decoded %dbit: Key=%08lX%08lX Key2=%04X (preamble=%d, sync@%d)",
        data_bits,
        (unsigned long)key_hi,
        (unsigned long)key_lo,
        data_16,
        preamble_ones,
        sync_pos);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

void subghz_protocol_decoder_peugot_v0_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderPeugotV0* instance = context;

    bool is_short = DURATION_DIFF(duration, subghz_protocol_peugot_v0_const.te_short) <=
                    subghz_protocol_peugot_v0_const.te_delta;
    bool is_long = DURATION_DIFF(duration, subghz_protocol_peugot_v0_const.te_long) <=
                   subghz_protocol_peugot_v0_const.te_delta;

    if(is_short) {
        if(instance->half_bit) {
            // Second SHORT completes a bit
            if(instance->bit_count < PEUGOT_V0_MAX_BITS) {
                subghz_bitbuf_set(instance->all_bits, instance->bit_count++, level ? 1 : 0);
            }
            instance->half_bit = false;
        } else {
            // First SHORT, wait for second
            instance->half_bit = true;
        }
    } else if(is_long) {
        // LONG = one complete bit
        if(instance->bit_count < PEUGOT_V0_MAX_BITS) {
            subghz_bitbuf_set(instance->all_bits, instance->bit_count++, level ? 1 : 0);
        }
        instance->half_bit = false;
    } else {
        // Gap or invalid timing → process collected bits and reset
        if(instance->bit_count > 0) {
            peugot_v0_process_frame(instance);
            instance->bit_count = 0;
            instance->half_bit = false;
        }
    }

    // Buffer full → process
    if(instance->bit_count >= PEUGOT_V0_MAX_BITS) {
        peugot_v0_process_frame(instance);
        instance->bit_count = 0;
        instance->half_bit = false;
    }
}



// ============================================================================
// SERIALIZATION
// ============================================================================

SubGhzProtocolStatus subghz_protocol_decoder_peugot_v0_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderPeugotV0* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    // Write extra 16 bits as Key2
    uint32_t key2 = instance->data_16;
    if(!flipper_format_write_uint32(flipper_format, "Key2", &key2, 1)) {
        FURI_LOG_E(TAG, "Failed to write Key2");
        return SubGhzProtocolStatusError;
    }

    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_peugot_v0_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderPeugotV0* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_deserialize(&instance->generic, flipper_format);

    if(ret != SubGhzProtocolStatusOk) {
        FURI_LOG_E(TAG, "Failed to deserialize base fields");
        return ret;
    }

    if(instance->generic.data_count_bit != PEUGOT_V0_DATA_BITS) {
        FURI_LOG_E(TAG, "Invalid bit count: %lu", (unsigned long)instance->generic.data_count_bit);
        return SubGhzProtocolStatusErrorValueBitCount;
    }

    // Read Key2 (optional, for backwards compat)
    uint32_t key2 = 0;
    flipper_format_read_uint32(flipper_format, "Key2", &key2, 1);
    instance->data_16 = (uint16_t)(key2 & 0xFFFF);

    instance->generic.protocol_name = instance->base.protocol->name;
    return SubGhzProtocolStatusOk;
}

/**
 * Build one complete frame into the upload buffer.
 * Signal: [80x SHORT pairs (preamble)][Manchester FE sync + 80 data bits][END GAP]
 */
