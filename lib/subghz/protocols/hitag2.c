/**
 * Hitag2 RKE — NXP PCF7946/7947 Rolling-Code Remote Keyless Entry
 *
 * Reference: Garcia, Oswald, Kasper, Pavlidès,
 *   "Lock It and Still Lose It — On the (In)Security of Automotive RKE Systems",
 *   USENIX Security 2016, Figure 11.
 *
 * Used by many manufacturers (Alfa Romeo, Citroën, Dacia, Fiat, Ford, Lancia,
 * Mitsubishi, Nissan, Opel/Vauxhall, Peugeot, Renault, …). Cipher = Hitag2
 * (48-bit LFSR + nonlinear filter). The rolling code can be recovered with the
 * fast correlation attack in tools/hitag2/hitag2_crack.{c,py} from ≥4 traces.
 *
 * Physical (verified on Ford Ka 2016, 433.92 MHz OOK):
 *   Manchester, te_short≈240us / te_long≈480us (G.E. Thomas).
 *   Preamble: run of 1-bits (carrier sync).
 *
 * Packet (103 bits, Figure 11):
 *   [marker 16b][UID 32b][btn 4b][lctr 10b][ks 32b][0 1b][chk 8b]
 *   - UID  : 32-bit identifier, constant per remote
 *   - btn  : 4-bit button id (Ford Ka: 0x3=Lock, 0x5=Unlock, 0x9=Trunk)
 *   - lctr : low 10 bits of the 28-bit rolling counter (cleartext, increments)
 *   - ks   : 32-bit Hitag2 keystream (the encrypted rolling part)
 *   - chk  : XOR-parity over the 10 bytes preceding it (bits 15..94)
 *
 * Sync is self-validating via the checksum: a frame is accepted only where the
 * XOR of 10 consecutive bytes equals the following byte. This removes the
 * ±1-bit preamble-length ambiguity inherent to a variable-length carrier run.
 *
 * Decode-only (dev): the rolling code is encrypted with a per-remote key, so
 * there is no roundtrip encoder.
 */

#include "hitag2.h"

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#include <furi.h>
#include <furi_hal.h>
#include "common.h"
#include <lib/subghz/blocks/bitbuf.h>

#define TAG "SubGhzProtocolHitag2"

#define HITAG2_DATA_BITS     103
#define HITAG2_ALL_BITS_MAX  400
/* checksum spans 10 bytes; smallest acceptable frame = checksum window + chk */
#define HITAG2_MIN_BITS      88
/* carrier-preamble 1-bits required immediately before the frame (anti-alias) */
#define HITAG2_PREAMBLE_MIN  14
/* raw-pulse buffer per burst (Manchester ≈ 1.5 pulses/bit + preamble) */
#define HITAG2_MAXDUR        700
/* a space longer than this ends a burst (longer than any te_long across the
 * 90..260µs bitrate range we support) */
#define HITAG2_GAP           1500
#define HITAG2_MIN_PULSE     30
/* te_short search window for the per-burst histogram (µs) */
#define HITAG2_TE_MIN        70
#define HITAG2_TE_MAX        320

static const SubGhzBlockConst subghz_protocol_hitag2_const = {
    .te_short = 240,
    .te_long = 480,
    .te_delta = 120,
    .min_count_bit_for_found = HITAG2_MIN_BITS,
};

// ============================================================================
// DATA STRUCTURES
// ============================================================================

struct SubGhzProtocolDecoderHitag2 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    // Raw signed pulse durations of the current burst (+ = high, - = low).
    // te_short is estimated per burst, so a single decoder handles the whole
    // Hitag2 bitrate range (te≈90µs Fiat/Alfa .. te≈240µs Ford Ka).
    int16_t durs[HITAG2_MAXDUR];
    uint16_t dur_count;

    // Scratch demodulated bits (filled per polarity in process()).
    uint8_t all_bits[SUBGHZ_BITBUF_BYTES(HITAG2_ALL_BITS_MAX)];

    uint32_t uid;   // 32-bit identifier
    uint8_t  btn;   // 4-bit button id
    uint16_t lctr;  // 10-bit cleartext counter (low bits)
    uint32_t ks;    // 32-bit keystream
    uint8_t  chk;   // 8-bit parity byte
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderHitag2);


struct SubGhzProtocolEncoderHitag2 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

// ============================================================================
// HELPERS
// ============================================================================

static uint32_t hitag2_bits_to_int(const uint8_t* bits, int lo, int len) {
    uint32_t v = 0;
    for(int i = 0; i < len; i++) v = (v << 1) | (subghz_bitbuf_get(bits, lo + i) & 1);
    return v;
}

// ============================================================================
// FRAME PROCESSING — checksum-validated sync
//
// For a candidate start position 'cs' (= bit 15 of the packet, one bit before
// the UID), the byte-XOR parity over bytes [cs .. cs+80) must equal the byte at
// [cs+80 .. cs+88). When it matches, the fields are extracted relative to cs:
//   UID  = bits[cs+1  .. cs+33]
//   btn  = bits[cs+33 .. cs+37]
//   lctr = bits[cs+37 .. cs+47]
//   ks   = bits[cs+47 .. cs+79]
//   chk  = bits[cs+80 .. cs+88]
// ============================================================================

static bool subghz_protocol_decoder_hitag2_find_and_emit(
    SubGhzProtocolDecoderHitag2* instance, int n) {
    const uint8_t* b = instance->all_bits;
    if(n < HITAG2_MIN_BITS) return false;

    for(int cs = 0; cs + 88 <= n; cs++) {
        // (1) require the fixed '0' bit between ks and chk (Figure 11)
        if(subghz_bitbuf_get(b, cs + 79) != 0) continue;

        // (2) verify the actual 16-bit marker WORD = 0x0001 (Figure 11) ending at
        //     cs (bits[cs-15..cs]), not merely a 14-equal-bit run. cs is the last
        //     pre-UID bit; UID starts at cs+1. The carrier preamble demods to the
        //     marker's leading zeros, and the frame is checksum-valid in exactly
        //     one polarity — the same polarity in which the marker reads 0x0001
        //     (the inverted polarity yields 0xFFFE). Pins polarity and removes the
        //     ±1-bit XOR-parity aliases the value-agnostic run check let through.
        if(cs < 15) continue;
        uint16_t marker = (uint16_t)hitag2_bits_to_int(b, cs - 15, 16);
        if(marker != 0x0001) continue;

        // (3) XOR-parity checksum over the 10 bytes preceding chk
        uint8_t x = 0;
        for(int i = cs; i < cs + 80; i += 8) {
            x ^= (uint8_t)hitag2_bits_to_int(b, i, 8);
        }
        uint8_t chk = (uint8_t)hitag2_bits_to_int(b, cs + 80, 8);
        if(x != chk) continue;

        // (4) reject the all-zero / all-one UID — a real remote never has it,
        //     and an empty bit run trivially satisfies the XOR-parity.
        uint32_t uid = hitag2_bits_to_int(b, cs + 1, 32);
        if(uid == 0 || uid == 0xFFFFFFFF) continue;

        // Valid Hitag2 frame at cs
        instance->uid = uid;
        instance->btn = (uint8_t)hitag2_bits_to_int(b, cs + 33, 4);
        instance->lctr = (uint16_t)hitag2_bits_to_int(b, cs + 37, 10);
        instance->ks = hitag2_bits_to_int(b, cs + 47, 32);
        instance->chk = chk;

        // (5) lctr/keystream plausibility bound: a genuine rolling frame never
        //     carries an all-zero or all-one counter+keystream (a demod artefact,
        //     not a real remote). Keep scanning for a valid frame if it fails.
        if((instance->lctr == 0 && instance->ks == 0) ||
           (instance->lctr == 0x3FF && instance->ks == 0xFFFFFFFF)) {
            continue;
        }

        // generic.data = first 64 bits from UID (UID:32 | btn:4 | lctr:10 | ks_hi:18)
        instance->generic.data = ((uint64_t)instance->uid << 32) |
                                 ((uint64_t)(instance->btn & 0xF) << 28) |
                                 ((uint64_t)(instance->lctr & 0x3FF) << 18) |
                                 (uint64_t)(instance->ks >> 14);
        instance->generic.data_count_bit = HITAG2_DATA_BITS;
        instance->generic.serial = instance->uid;
        instance->generic.btn = instance->btn;
        instance->generic.cnt = instance->lctr;

        instance->decoder.decode_data = instance->generic.data;
        instance->decoder.decode_count_bit = HITAG2_DATA_BITS;

        FURI_LOG_I(
            TAG,
            "Hitag2: UID=%08lX btn=%X lctr=%03X ks=%08lX chk=%02X",
            (unsigned long)instance->uid,
            instance->btn,
            instance->lctr,
            (unsigned long)instance->ks,
            instance->chk);

        if(instance->base.callback) {
            instance->base.callback(&instance->base, instance->base.context);
        }
        return true;
    }
    return false;
}

// ============================================================================
// Per-burst te_short estimation + Manchester demodulation.
//
// Hitag2 RKE bitrate varies a lot by manufacturer (te≈90µs Fiat/Alfa ..
// te≈240µs Ford Ka). Instead of a fixed timing we estimate te_short from a
// histogram of the burst's pulse widths (the half-bit is the most common
// pulse), then run a stateless G.E.-Thomas Manchester decode. Both polarities
// are tried; the first that yields a checksum-valid frame wins.
// ============================================================================

static uint32_t hitag2_estimate_te(const int16_t* durs, int count) {
    // 10µs-bin histogram over the plausible te_short window; return the
    // most populated bin center.
    enum { BINS = (HITAG2_TE_MAX - HITAG2_TE_MIN) / 10 + 1 };
    uint16_t hist[BINS];
    for(int i = 0; i < BINS; i++) hist[i] = 0;
    for(int i = 0; i < count; i++) {
        int d = durs[i] < 0 ? -durs[i] : durs[i];
        if(d < HITAG2_TE_MIN || d > HITAG2_TE_MAX) continue;
        hist[(d - HITAG2_TE_MIN) / 10]++;
    }
    int best = 0;
    for(int i = 1; i < BINS; i++)
        if(hist[i] > hist[best]) best = i;
    return (uint32_t)(HITAG2_TE_MIN + best * 10 + 5);
}

static int hitag2_demod(
    const int16_t* durs, int count, uint32_t te_short, bool polarity, uint8_t* out) {
    uint32_t te_long = 2 * te_short;
    uint32_t delta = te_short * 45 / 100;
    if(delta < 40) delta = 40;
    int n = 0;
    bool half = false;
    for(int i = 0; i < count && n < HITAG2_ALL_BITS_MAX - 1; i++) {
        bool level = durs[i] > 0;
        uint32_t d = durs[i] < 0 ? (uint32_t)(-durs[i]) : (uint32_t)durs[i];
        bool is_long = DURATION_DIFF(d, te_long) <= delta;
        bool is_short = DURATION_DIFF(d, te_short) <= delta;
        uint8_t bl = polarity ? (level ? 1 : 0) : (level ? 0 : 1);
        if(is_long) {
            subghz_bitbuf_set(out, n++, bl);
            half = false;
        } else if(is_short) {
            if(half) {
                subghz_bitbuf_set(out, n++, bl);
                half = false;
            } else {
                half = true;
            }
        } else {
            half = false; // noise/invalid: drop, keep scanning (checksum re-syncs)
        }
    }
    return n;
}

static void subghz_protocol_decoder_hitag2_process_burst(SubGhzProtocolDecoderHitag2* instance) {
    if(instance->dur_count < HITAG2_MIN_BITS) return;
    uint32_t te = hitag2_estimate_te(instance->durs, instance->dur_count);
    for(int pol = 0; pol < 2; pol++) {
        int n = hitag2_demod(instance->durs, instance->dur_count, te, pol != 0, instance->all_bits);
        if(subghz_protocol_decoder_hitag2_find_and_emit(instance, n)) return;
    }
    FURI_LOG_D(TAG, "No Hitag2 frame in burst (%d pulses, te≈%lu)",
        instance->dur_count, (unsigned long)te);
}

// ============================================================================
// DECODER: feed — collect raw burst pulses, decode on the trailing gap.
// ============================================================================

void subghz_protocol_decoder_hitag2_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderHitag2* instance = context;

    if(duration >= HITAG2_GAP) {
        // burst boundary
        subghz_protocol_decoder_hitag2_process_burst(instance);
        instance->dur_count = 0;
        return;
    }
    if(duration < HITAG2_MIN_PULSE) {
        return; // ignore glitches without breaking the burst
    }
    if(instance->dur_count >= HITAG2_MAXDUR) {
        subghz_protocol_decoder_hitag2_process_burst(instance);
        instance->dur_count = 0;
    }
    int32_t v = (int32_t)duration;
    if(v > 32000) v = 32000;
    instance->durs[instance->dur_count++] = (int16_t)(level ? v : -v);
}

// ============================================================================
// PROTOCOL VTABLES
// ============================================================================


const SubGhzProtocolDecoder subghz_protocol_hitag2_decoder = {
    .alloc = subghz_protocol_decoder_hitag2_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_hitag2_feed,
    .reset = subghz_protocol_decoder_hitag2_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_hitag2_serialize,
    .deserialize = subghz_protocol_decoder_hitag2_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_hitag2_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

const SubGhzProtocol subghz_protocol_hitag2 = {
    .name = SUBGHZ_PROTOCOL_HITAG2_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_hitag2_decoder,
    .encoder = &subghz_protocol_hitag2_encoder,
};

// ============================================================================
// DECODER: alloc / free / reset
// ============================================================================

void* subghz_protocol_decoder_hitag2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderHitag2* instance = malloc(sizeof(SubGhzProtocolDecoderHitag2));
    instance->base.protocol = &subghz_protocol_hitag2;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}



void subghz_protocol_decoder_hitag2_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderHitag2* instance = context;
    instance->decoder.parser_step = 0;
    instance->decoder.te_last = 0;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->dur_count = 0;
    instance->generic.data_count_bit = 0;
}

// ============================================================================
// DECODER: hash / serialize / deserialize
// ============================================================================



SubGhzProtocolStatus subghz_protocol_decoder_hitag2_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderHitag2* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret == SubGhzProtocolStatusOk) {
        uint32_t ks = instance->ks;
        flipper_format_write_uint32(flipper_format, "Ks", &ks, 1);
    }
    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_hitag2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderHitag2* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_hitag2_const.min_count_bit_for_found);
    if(ret == SubGhzProtocolStatusOk) {
        uint32_t ks = 0;
        flipper_format_read_uint32(flipper_format, "Ks", &ks, 1);
        instance->ks = ks;
        instance->uid = instance->generic.serial;
        instance->btn = instance->generic.btn;
        instance->lctr = (uint16_t)instance->generic.cnt;
    }
    return ret;
}

// ============================================================================
// DECODER: get_string
// ============================================================================

