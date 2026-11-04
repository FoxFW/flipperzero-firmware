/*
 * Packer Structure:
 *   - Preamble: ~250 short pulses (~125μs each, alternating high/low).
 *   - Transit: 1500μs lo + 1000μs hi pulse. 
 *   - Sync word: 15 short lo-hi + 1 short hi-lo (16 Manchester bits).
 *   - Data: 88 Manchester bits.
 *   - Check: transit + sync word + 10 Manchester bits of extra data.
 *   - 1 is short hi-lo, 0 is short lo-hi ?
 *   - While holding the button max emission time is 3 seconds.
 *   - Preamble goes once, then transit & sync word & data repeat 3 times.
 *   - Then goes check part which repeats more than 50 times with ~21500μs pause.
 *   - Approx half of check repeats is not visible on .sal records:
 *   - this could be a drifting keyfob radio or receiving issues.
 *
 * Constants from largus.cpp source file:
 *   - PULSE_MIN   50 uS    
 *   - PULSE_MID   180 uS    
 *   - PULSE_MAX   300 uS   
 *   - PRE_COUNT   10    
 *   - BITS_TOTAL  88   
 *   - CRC_INDEX   10    
 *   - DATA_SIZE   11 bytes
 *   - TE          125    
 *   - PRE_CNT     245   
 *   - SYNC_HIGH   1500 uS   
 *   - SYNC_LOW    1000 uS  
 *   - SYNC_NUM    15  
 *   - DATA_LEN    11 bytes    
 *   - CRC_LEN     12    
 *   - BITS        8   
 */
#include "renault_v1.h"

#include "common.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"
#include "renault_v1_crypto.h"
#include <lib/toolbox/manchester_decoder.h>

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#include "../blocks/custom_btn_i.h"

#define TAG "RenaultV1"

static const SubGhzBlockConst subghz_protocol_renault_v1_const = {
    .te_short = 125, // Manchester short pulse
    .te_long = 250, // Manchester long pulse, 2× short
    .te_delta = 100, // Tolerance window for FEED
    .min_count_bit_for_found = 88, // Total data bits
};

#define RENAULT_V1_LO_GAP 1500
#define RENAULT_V1_HI_GAP 1000
#define RENAULT_V1_PRE_COUNT       250
#define RENAULT_V1_SYNC_BIT_COUNT 16
#define RENAULT_V1_CHECK_BIT_COUNT 10
#define RENAULT_V1_SYNC_WORD       0x0001
#define RENAULT_V1_DATA_1_SIZE     64
#define RENAULT_V1_DATA_2_SIZE     24
#define RENAULT_V1_DATA_REPEAT     3
#define RENAULT_V1_TAIL_REPEAT_GAP 21500
#define RENAULT_V1_CUSTOM_BTN_MAX  2 // D-pad custom buttons: this fob has only Lock(0x1) + Unlock(0x2)
#define RENAULT_V1_COUNTER_BITS    16 // rolling counter (push_clock) width, carved from the plaintext IV
#define RENAULT_V1_COUNTER_MAX     ((1u << RENAULT_V1_COUNTER_BITS) - 1)
#define RENAULT_V1_PULSE_MIN       50 // Below this: envelope glitch -- ignored, does NOT reset a frame
#define RENAULT_V1_PULSE_MAX \
    520 // long ceiling: 2TE-LOW stretches to ~499 under OOK duty asymmetry

/* Adaptive short/long split, tracked per level. No fixed threshold serves both populations at once:
 * genuine fobs put 1TE up to ~204 and 2TE from ~205, while a Pandora emulating a fob compresses its
 * 2TE HIGH down to ~192. Any single constant either clips the Pandora 2TE (its frames die mid-payload)
 * or lets an OEM 2TE-LOW read as short. So each frame tracks the 1TE duration SEPARATELY for HIGH and
 * LOW -- they differ by ~40us from OOK duty asymmetry (carrier-on shorter than carrier-off) -- seeded,
 * then refined from every pulse it classes as short, with the boundary at 1.5x that estimate. The
 * 16-bit sync word is all-short and calibrates the estimate before the data bits arrive.
 *
 * The threshold is CLAMPED to [RENAULT_V1_THR_MIN, RENAULT_V1_THR_MAX]: the floor keeps a compressed Pandora 2TE-HIGH (~192)
 * reading long, the ceiling stops the LOW estimate drifting up until an OEM 2TE-LOW (~205) reads short.
 * Swept over all 25 captures this strictly beat the best fixed value on EVERY file (Pandora 5 -> 23
 * valid frames, fobs equal-or-better, min frames/capture unchanged at 2). False frames stayed ZERO --
 * the sync+CRC gate, not this threshold, is what rejects garbage. */
#define RENAULT_V1_TE_SEED_HI 120 // 1TE HIGH is the shorter half (carrier-on, measured ~104..122)
#define RENAULT_V1_TE_SEED_LO 150 // 1TE LOW is the longer half (carrier-off, measured ~145)
#define RENAULT_V1_TE_TRACK_MIN \
    70 // clamp on the tracked estimate so a noise run can't drag it away
#define RENAULT_V1_TE_TRACK_MAX    185
#define RENAULT_V1_THR_MIN         150
#define RENAULT_V1_THR_MAX         210
// Transit-gap acceptance windows. Measured across all six fobs: gap LOW 1250..2094, gap HIGH 851..1075.
// Wide on purpose -- the gap drifts a lot while a button is held, and nothing in the data is this long.
#define RENAULT_V1_GAP_LO_MIN      1150
#define RENAULT_V1_GAP_LO_MAX      2200
#define RENAULT_V1_GAP_HI_MIN      800
#define RENAULT_V1_GAP_HI_MAX      1150

/* Exact LevelDuration count get_upload() produces, written as the layout itself so the buffer and the
 * bound check can never drift apart. Per frame: a 2-entry transit gap plus 2 entries per Manchester
 * bit. Preamble is one pair per RENAULT_V1_PRE_COUNT; each data frame carries sync+88 bits; each tail frame
 * carries sync+10 bits and a trailing silence entry. An enum, not a macro, so it needs no line
 * continuations and stays a typed constant. Works out to 1295. */
enum {
    RENAULT_V1_DATA_FRAME_ENTRIES =
        2 + (RENAULT_V1_SYNC_BIT_COUNT + RENAULT_V1_DATA_1_SIZE + RENAULT_V1_DATA_2_SIZE) * 2,
    RENAULT_V1_TAIL_FRAME_ENTRIES =
        2 + (RENAULT_V1_SYNC_BIT_COUNT + RENAULT_V1_CHECK_BIT_COUNT) * 2 + 1,
    RENAULT_V1_UPLOAD_SIZE = RENAULT_V1_PRE_COUNT * 2 +
                             RENAULT_V1_DATA_REPEAT * RENAULT_V1_DATA_FRAME_ENTRIES +
                             RENAULT_V1_DATA_REPEAT * RENAULT_V1_TAIL_FRAME_ENTRIES,
};

struct SubGhzProtocolDecoderRenaultV1 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_saved_state;
    /* Inverted 16-bit Hitag-2 sync word, captured at RENAULT_V1_SYNC_BIT_COUNT and then used
     * as a frame gate and as hash input. It used to squat on SubGhzBlockGeneric.disc, whose
     * own comment describes a KeeLoq 12-bit discriminator -- one shared field carrying two
     * unrelated meanings, with only one of them documented. Decoder-local, never serialized. */
    uint16_t sync;
    uint16_t te_hi; // running 1TE estimate for HIGH pulses, reseeded each frame
    uint16_t te_lo; // running 1TE estimate for LOW pulses, reseeded each frame
};
SUBGHZ_ASSERT_DECODER_PREFIX(SubGhzProtocolDecoderRenaultV1);

struct SubGhzProtocolEncoderRenaultV1 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderRenaultV1);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderRenaultV1);

SUBGHZ_ASSERT_ENCODER_PREFIX(SubGhzProtocolEncoderRenaultV1);

typedef enum {
    RenaultV1DecoderStepReset = 0,
    RenaultV1DecoderStepCheckPreamble,
    RenaultV1DecoderStepCheckSync,
    RenaultV1DecoderStepDecodeData,
} RenaultV1DecoderStep;

const SubGhzProtocolDecoder subghz_protocol_renault_v1_decoder = {
    .alloc = subghz_protocol_decoder_renault_v1_alloc,
    .free = subghz_protocol_decoder_generic_free,

    .feed = subghz_protocol_decoder_renault_v1_feed,
    .reset = subghz_protocol_decoder_renault_v1_reset,

    .get_hash_data = subghz_protocol_decoder_renault_v1_get_hash_data,
    .serialize = subghz_protocol_decoder_renault_v1_serialize,
    .deserialize = subghz_protocol_decoder_renault_v1_deserialize,
    .get_string = subghz_protocol_decoder_renault_v1_get_string,
};

/* host bridge: firmware functions/state the SD encoder uses (handed over at load) */
typedef struct {
    __typeof__(&subghz_protocol_renault_v1_encrypt) subghz_protocol_renault_v1_encrypt;
} enc_renault_v1_host_t;
static const enc_renault_v1_host_t enc_renault_v1_host = {
    .subghz_protocol_renault_v1_encrypt = subghz_protocol_renault_v1_encrypt,
};
static const SubGhzEncoderExternal subghz_protocol_renault_v1_encoder_ext = SUBGHZ_ENCODER_EXTERNAL_HOST("enc_renault_v1", &enc_renault_v1_host);

const SubGhzProtocol subghz_protocol_renault_v1 = {
    .name = SUBGHZ_PROTOCOL_RENAULT_V1_NAME,
    /* 1295 upload entries (RENAULT_V1_UPLOAD_SIZE) x 4 B = 5180 B in ONE malloc, plus the
     * ~5 KB margin every other heavy protocol uses, rounded up to 512: mazda_v2 640 entries ->
     * 7680, kia_v3_v4 2628 -> 15872, ford_v1 6452 -> 31744. Shipped commented out, which left
     * this the WORST of the registered protocols running on
     * SUBGHZ_TX_HEAP_DEFAULT (6144, documented for uploads <= 1 KB) -- secplus_v2 was over it
     * too and now carries its own. Here it was a 5.2 KB contiguous
     * allocation admitted on a 6 KB check, and pvPortMalloc reboots rather than returning
     * NULL. The commented value was ford_v1's, copied with the line; at 3x the real need it
     * would over-gate instead, refusing TX whenever contiguous free heap is under 31 KB --
     * the same failure the VAG special case in subghz_i.c had to undo. */
    .tx_heap_budget = 10752,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 | SubGhzProtocolFlag_315 |
            SubGhzProtocolFlag_FM | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send |
            SubGhzProtocolFlag_Bruteforce,

    .decoder = &subghz_protocol_renault_v1_decoder,
    .encoder = &subghz_protocol_renault_v1_encoder_ext.vtable,
};

/* Derive serial and button from the payload. The 88-bit payload is 11 bytes; generic.data holds
 * bytes 0..7 MSB-first, so bytes 0..3 are the serial and byte 4 carries the button in its HIGH
 * nibble (measured across all six fobs: Lock=1, Unlock=2, matching Largus.cpp's `pushkey<<4`).
 *
 * Derived HERE rather than in feed() on purpose: subghz_block_generic_deserialize restores only the
 * bit count and Key, and decoder slots are reused WITHOUT reset() between file loads, so anything set
 * only while receiving would be stale or zero for a loaded .sub. This runs from serialize(),
 * deserialize() AND get_string(), which is every path that can display or re-encode. */
static void subghz_protocol_renault_v1_check_remote_controller(SubGhzBlockGeneric* instance) {
    instance->serial = (uint32_t)(instance->data >> 32);
    instance->btn = (uint8_t)((instance->data >> 28) & 0x0F);
    /* Offer the D-pad button swap ONLY once recovered: on an un-recovered signal the encoder can only
     * replay the captured payload (it cannot re-encrypt a different button), so a swap would send the
     * original button while the on-screen label shows the chosen one. set_max(0) hides the D-pad then.
     * status==1 only: a status of 2 (BF not found) is NOT a recovery, so it must not unlock the swap. */
    subghz_custom_btn_set_max((instance->status == 1) ? RENAULT_V1_CUSTOM_BTN_MAX : 0);
    /* The rolling counter is NOT in the raw ciphertext -- it is a plaintext field of the DECRYPTED IV
     * that recovery leaves in generic.seed, so it exists only once status is set. Host-checked over 44
     * consecutive presses of two fobs, this 16-bit field steps by exactly 1 per press (fob1 2A7A..2AA5,
     * fob3 1208..1233) while the fixed part holds. Deriving it here, from the one stored Seed, keeps
     * every display/serialize/deserialize path consistent and means the counter is never written to file.
     * Layout of the decrypted IV: iv0 low nibble = button, iv0 high nibble | iv1 | iv2 low nibble =
     * counter, iv2 high nibble | iv3 = fixed. status==1 only -- status 2 (BF not found) has no IV. */
    if(instance->status == 1) {
        const uint32_t iv = instance->seed;
        instance->cnt =
            ((iv >> 28) & 0x0F) | (((iv >> 16) & 0xFF) << 4) | (((iv >> 8) & 0x0F) << 12);
    } else {
        instance->cnt = 0;
    }
}

// Pack the 4-byte decrypted IV (MSB-first) into a 32-bit word; shared by bf_apply, the encoder re-seed
// and the bruteforce scene's "Seed:" display so the idiom lives once.
uint32_t renault_v1_iv_to_u32(const uint8_t iv[4]) {
    return ((uint32_t)iv[0] << 24) | ((uint32_t)iv[1] << 16) | ((uint32_t)iv[2] << 8) | iv[3];
}

// ============================================================================
// Helpers
// ============================================================================

/* Remap table [custom_id-1][orig_pos] over renault_v1_btns. OK re-sends the captured button; UP forces
 * Lock, DOWN forces Unlock (LEFT/RIGHT unused). Every orig_pos maps to the same target because the
 * choice is absolute, not relative to what was captured. Unlike the old Ford-template map, this is now
 * live: get_upload re-encrypts (ciphertext + CRC) for the chosen button, so the swap is a real frame. */
/* One Manchester symbol pair. Which way round is NOT a matter of convention here: the decoder is
 * validated against real fobs, so the encoder is written to match it. Under this mapping the bits
 * recovered by the decoder are the COMPLEMENT of the ones written -- which is why the payload below
 * is emitted verbatim rather than inverted, and why RENAULT_V1_SYNC_WORD is written plain and still reads back
 * as 0x0001. Proven by round-tripping a captured frame through both, and by comparing the generated
 * waveform against the fob's own capture symbol for symbol. */
/* @p count bits of @p value, MSB first -- the order the fob transmits.
 *
 * This replaces six hand-unrolled copies that all read `bit_read(x, i - 1)`. That index is wrong
 * twice over: at i = 0 it shifts by -1, which is undefined behaviour (in practice a 0 bit), and the
 * remaining bits then come out LSB-ward, so the sync word went out as 0100...0 instead of 0000...1
 * and the payload was scrambled. Nothing a receiver could decode. */
void* subghz_protocol_decoder_renault_v1_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderRenaultV1), &subghz_protocol_renault_v1);
}

void subghz_protocol_decoder_renault_v1_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderRenaultV1* instance = context;
    instance->decoder.parser_step = RenaultV1DecoderStepReset;
    manchester_advance(
        instance->manchester_saved_state,
        ManchesterEventReset,
        &instance->manchester_saved_state,
        NULL);
}

/* Unpack the 88-bit payload into 11 bytes: generic.data holds bytes 0..7 (MSB first, inverted as
 * decoded), generic.data_2 holds bytes 8..10. Shared by the CRC gate and the bruteforce init so the two
 * never drift. */
static void renault_v1_pack_payload(uint64_t data, uint64_t data_2, uint8_t out[11]) {
    for(int i = 0; i < 8; i++)
        out[i] = (uint8_t)(data >> (56 - 8 * i));
    for(int i = 0; i < 3; i++)
        out[8 + i] = (uint8_t)(data_2 >> (16 - 8 * i));
}

/* Largus data_crc, verified against real captures of all six fobs: byte 10 == XOR(byte0..byte9). Gating
 * the callback on this (plus the fixed sync word) stops a mis-phased or noisy frame from surfacing as a
 * bogus key -- the "catches 2 / garbage / false PSA" symptoms. */
static bool renault_v1_payload_crc_ok(uint64_t data, uint64_t data_2) {
    uint8_t p[11];
    renault_v1_pack_payload(data, data_2, p);
    uint8_t crc = 0;
    for(int i = 0; i < 10; i++)
        crc ^= p[i];
    return crc == p[10];
}

void subghz_protocol_decoder_renault_v1_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderRenaultV1* instance = context;

    switch(instance->decoder.parser_step) {
    case RenaultV1DecoderStepReset:
        /* Frame start = the transit gap, a LONG low (~1500) then LONG high (~1000). Detected on its
         * own, NOT off the back of the preamble, so every repeat -- 2nd, 3rd, the tail -- re-arms
         * here on its own gap; the preamble ships only once, ahead of the first frame. The gap is
         * unmistakable: no data symbol is anywhere near this long.
         *
         * Gating on the preamble INSTEAD was measured across all six fobs' captures, and it is not a
         * free win either way. Requiring it before every frame is simply wrong: the preamble runs
         * 508..511 short pulses ahead of the FIRST gap and 1..6 ahead of the others, so that form
         * decodes only frame 1 -- and on five of the captures frame 1 is the one that fails CRC while
         * frames 2/3 are clean, i.e. it recovers nothing at all there (126 valid frames vs 600).
         * Arming ONCE per burst and re-triggering per gap scores identically (600) on captures that
         * start at the beginning, and costs a third of the decode attempts; it was NOT taken because
         * it drops a whole burst whenever the preamble was never heard -- the preamble is ~64 ms out
         * of a ~3 s hold, so tuning in mid-burst (AGC settling, hopping, RX opened after the press)
         * is the common case, not the corner one. False triggers are handled at the other end
         * instead: 16 sync bits AND the payload CRC must both agree before anything is emitted. */
        if((!level) && (duration >= RENAULT_V1_GAP_LO_MIN) &&
           (duration <= RENAULT_V1_GAP_LO_MAX)) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = RenaultV1DecoderStepCheckSync;
        }
        break;

    case RenaultV1DecoderStepCheckSync:
        // Second half of the transit gap: the LONG high that follows the LONG low.
        if(level && (duration >= RENAULT_V1_GAP_HI_MIN) && (duration <= RENAULT_V1_GAP_HI_MAX) &&
           (instance->decoder.te_last >= RENAULT_V1_GAP_LO_MIN) &&
           (instance->decoder.te_last <= RENAULT_V1_GAP_LO_MAX)) {
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            /* Manchester phase: enter at Start1, NOT the toolbox reset state (Mid1). The gap ends
             * HIGH and the first sync half-bit is LOW; Start1 is the alignment under which the 0x0001
             * sync and the 88 data bits decode. Mid1 mis-phases and the frame dies ~26 bits in. */
            instance->manchester_saved_state = ManchesterStateStart1;
            instance->te_hi = RENAULT_V1_TE_SEED_HI;
            instance->te_lo = RENAULT_V1_TE_SEED_LO;
            instance->decoder.parser_step = RenaultV1DecoderStepDecodeData;
        } else {
            // Not the expected gap-high. Re-arm if THIS pulse is itself a fresh gap-low.
            instance->decoder.parser_step = RenaultV1DecoderStepReset;
            if((!level) && (duration >= RENAULT_V1_GAP_LO_MIN) &&
               (duration <= RENAULT_V1_GAP_LO_MAX)) {
                instance->decoder.te_last = duration;
                instance->decoder.parser_step = RenaultV1DecoderStepCheckSync;
            }
        }
        break;

    case RenaultV1DecoderStepDecodeData: {
        /* A pulse shorter than RENAULT_V1_PULSE_MIN = envelope glitch. Ignore it, do NOT tear the frame down -- one runt
         * mid-frame otherwise costs the whole capture. (The subghz worker already coalesces <30us;
         * this covers the residue.) */
        if(duration < RENAULT_V1_PULSE_MIN) break;

        if(duration > RENAULT_V1_PULSE_MAX) {
            /* Longer than any data symbol -> the frame is over. Re-arm on this very pulse if it is
             * already the next frame's gap-low, so back-to-back repeats never miss a beat. */
            instance->decoder.parser_step = RenaultV1DecoderStepReset;
            if((!level) && (duration >= RENAULT_V1_GAP_LO_MIN) &&
               (duration <= RENAULT_V1_GAP_LO_MAX)) {
                instance->decoder.te_last = duration;
                instance->decoder.parser_step = RenaultV1DecoderStepCheckSync;
            }
            break;
        }

        /* Adaptive short/long decision against this level's tracked 1TE (see the note by the
         * RENAULT_V1_TE_SEED_* defines). Threshold = 1.5x te, clamped; a short pulse also refines the
         * estimate toward it. */
        uint16_t* te = level ? &instance->te_hi : &instance->te_lo;
        uint32_t thr = ((uint32_t)*te * 3u) / 2u;
        if(thr < RENAULT_V1_THR_MIN) thr = RENAULT_V1_THR_MIN;
        if(thr > RENAULT_V1_THR_MAX) thr = RENAULT_V1_THR_MAX;

        ManchesterEvent event;
        if(duration <= thr) {
            event = level ? ManchesterEventShortHigh : ManchesterEventShortLow;
            uint32_t upd = ((uint32_t)*te * 7u + duration) / 8u;
            if(upd < RENAULT_V1_TE_TRACK_MIN) upd = RENAULT_V1_TE_TRACK_MIN;
            if(upd > RENAULT_V1_TE_TRACK_MAX) upd = RENAULT_V1_TE_TRACK_MAX;
            *te = (uint16_t)upd;
        } else {
            event = level ? ManchesterEventLongHigh : ManchesterEventLongLow;
        }

        bool data;
        if(!manchester_advance(
               instance->manchester_saved_state, event, &instance->manchester_saved_state, &data)) {
            break;
        }
        instance->decoder.decode_data = (instance->decoder.decode_data << 1) | data;
        instance->decoder.decode_count_bit++;

        if(instance->decoder.decode_count_bit == RENAULT_V1_SYNC_BIT_COUNT) {
            instance->sync = ~instance->decoder.decode_data;
            instance->decoder.decode_data = 0;
        } else if(
            instance->decoder.decode_count_bit ==
            (RENAULT_V1_SYNC_BIT_COUNT + RENAULT_V1_DATA_1_SIZE)) {
            instance->generic.data = ~instance->decoder.decode_data;
            instance->decoder.decode_data = 0;
        } else if(
            instance->decoder.decode_count_bit ==
            (RENAULT_V1_SYNC_BIT_COUNT + RENAULT_V1_DATA_1_SIZE + RENAULT_V1_DATA_2_SIZE)) {
            instance->generic.data_2 = (~instance->decoder.decode_data) & 0xFFFFFF;
            instance->generic.data_count_bit = (RENAULT_V1_DATA_1_SIZE + RENAULT_V1_DATA_2_SIZE);
            /* A newly decoded frame is raw ciphertext -- nothing is recovered for it yet. This decoder
             * instance is reused across signals, so clear the recovery marker and IV a previous recover
             * left on it; otherwise the next frame -- the SAME fob's next press (a different rolling
             * counter) or a different fob -- would be saved and shown with the prior IV and its Recover
             * button suppressed. The counter is derived from the IV in check_remote_controller, so
             * clearing the IV clears it too. Rolling codes change every press, so every frame must start
             * un-recovered and be brute-forced afresh. */
            instance->generic.status = 0;
            instance->generic.seed = 0;
            /* Two independent gates before the frame is trusted: the fixed sync word AND the payload's
             * own XOR checksum. The 10-bit tail is deliberately NOT decoded -- it is derived byte-for-
             * byte from payload bytes 4 and 5 (Largus package[11]/[12]) and carries nothing new. */
            const uint8_t family = (uint8_t)(instance->generic.data >> 32);
            if(((instance->sync & 0xFFFF) == RENAULT_V1_SYNC_WORD) &&
               ((family & 0xF0) != 0x30) &&
               renault_v1_payload_crc_ok(instance->generic.data, instance->generic.data_2)) {
                if(instance->base.callback)
                    instance->base.callback(&instance->base, instance->base.context);
            }
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = RenaultV1DecoderStepReset;
        }
    } break;
    }
}

static const char* renault_v1_get_name_button(uint8_t btn);

uint8_t subghz_protocol_decoder_renault_v1_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderRenaultV1* instance = context;
    uint8_t hash =
        subghz_protocol_blocks_xor_bytes((uint8_t*)&instance->sync, sizeof(uint16_t));
    hash ^= subghz_protocol_blocks_xor_bytes((uint8_t*)&instance->generic.data, sizeof(uint64_t));
    hash ^=
        subghz_protocol_blocks_xor_bytes((uint8_t*)&instance->generic.data_2, sizeof(uint64_t));
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_renault_v1_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderRenaultV1* instance = context;
    SubGhzProtocolStatus res =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    subghz_protocol_renault_v1_check_remote_controller(&instance->generic);

    /* No rewind here. flipper_format writes at the CURRENT stream cursor (no seek-to-end,
     * no insert-in-place), so rewinding to the start would make the following writes
     * overwrite the generic block's header (Filetype/Version/Frequency/Preset and the
     * "Protocol: " prefix). That produced a .sub with no valid header -- the file browser
     * rejected it on reopen (lost capture), and the RX-list label came up blank. With the
     * cursor left at END after subghz_block_generic_serialize(), these fields append and
     * the file stays well-formed. */
    if(!subghz_block_write_hex(flipper_format, "Key", instance->generic.data, sizeof(uint64_t))) {
        // FURI_LOG_E(TAG, "DSKIE"); // Decoder Serialize: Key insert error
        res = SubGhzProtocolStatusErrorParserOthers;
    }
    if(!subghz_block_write_hex(
           flipper_format, "Key_2", instance->generic.data_2, sizeof(uint64_t))) {
        // FURI_LOG_E(TAG, "DSK2IE"); // Decoder Serialize: Key_2 insert error
        res = SubGhzProtocolStatusErrorParserOthers;
    }
    /* Persist only the marker + the secret Seed (the 32-bit DECRYPTED Hitag2 IV a rolling-code TX
     * re-encrypts from; conventional "Seed" key, as FAAC SLH / KeeLoq). The counter is a plaintext slice
     * of that IV -- check_remote_controller re-derives it on load -- so it is not stored.
     * Recovered is (status==1): only a real recovery is a property of the signal. "BF not found" (2) is
     * a SESSION fact kept in RAM (never written), so a saved capture never claims BF-not-found forever. */
    uint8_t recovered = (instance->generic.status == 1) ? 1 : 0;
    if(!subghz_block_write_hex(flipper_format, "Recovered", recovered, sizeof(uint8_t))) {
        res = SubGhzProtocolStatusErrorParserOthers;
    }
    if(!subghz_block_write_hex(flipper_format, "Seed", instance->generic.seed, sizeof(uint32_t))) {
        res = SubGhzProtocolStatusErrorParserOthers;
    }
    // Disp: protocol name + button for receiver list
    subghz_block_write_disp(
        flipper_format,
        instance->generic.protocol_name,
        renault_v1_get_name_button(instance->generic.btn));

    return res;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_renault_v1_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderRenaultV1* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_renault_v1_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            // FURI_LOG_E(TAG, "DDBCCE"); //Decoder Deserialize: bit count check error
            break;
        }
        if(!flipper_format_rewind(flipper_format)) {
            // FURI_LOG_E(TAG, "DDRE"); //Decoder Deserialize: rewind error
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        if(!subghz_block_read_hex(flipper_format, "Key", &instance->generic.data, sizeof(uint64_t))) {
            // FURI_LOG_E(TAG, "DDKRE"); //Decoder Deserialize: Key read error
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        if(!subghz_block_read_hex(
               flipper_format, "Key_2", &instance->generic.data_2, sizeof(uint64_t))) {
            // FURI_LOG_E(TAG, "DDK2RE"); //Decoder Deserialize: Key_2 read error
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }

        /* Optional -- absent on a pre-recovery / live signal (default: not recovered, Recover offered).
         * Rewind before each read so a missing key does not strand the position. The counter is not
         * read -- check_remote_controller re-derives it from the Seed below. */
        instance->generic.status = 0;
        instance->generic.seed = 0;
        if(flipper_format_rewind(flipper_format))
            subghz_block_read_hex(
                flipper_format, "Recovered", &instance->generic.status, sizeof(uint8_t));
        if(flipper_format_rewind(flipper_format))
            subghz_block_read_hex(
                flipper_format, "Seed", &instance->generic.seed, sizeof(uint32_t));

        subghz_protocol_renault_v1_check_remote_controller(&instance->generic);
        ret = SubGhzProtocolStatusOk;
    } while(false);
    return ret;
}

static const char* renault_v1_get_name_button(uint8_t btn) {
    switch(btn) {
    case 0x0:
        return "Sync";
    case 0x1:
        return "Lock";
    case 0x2:
        return "Unlock";
    case 0x4:
        return "Trunk";
    case 0x8:
        return "Panic";
    default:
        return "??";
    }
}

void subghz_protocol_decoder_renault_v1_get_string(void* context, FuriString* output) {
    SubGhzProtocolDecoderRenaultV1* instance = context;
    const SubGhzDecodeInput input = {
        .decoder = context,
        .generic = &instance->generic,
        .extra = NULL,
        .extra_count = 0,
        .aux = NULL,
    };
    subghz_decode_plugin_get_string(instance->generic.protocol_name, &input, output);
}


/* Bruteforce hooks -- these live here, not in renault_v1_crypto.c, because they touch the file-local
 * decoder struct (the cipher itself is protocol-agnostic and stays in the crypto module). They mirror
 * the PSA bf_init/apply/needs shape so the shared bruteforce scene can drive either protocol. */
bool subghz_protocol_renault_v1_needs_bruteforce(void* decoder_context) {
    furi_assert(decoder_context);
    SubGhzProtocolDecoderBase* base = decoder_context;
    if(base->protocol != &subghz_protocol_renault_v1) return false;
    SubGhzProtocolDecoderRenaultV1* instance = decoder_context;
    /* Gate on a DEDICATED marker, never on a data value: the recovered counter or IV can each be a
     * legitimate zero, so `cnt == 0` would keep re-offering the button (and hide a real recovery).
     * generic.status is 0 until bf_apply sets it, exactly as PSA uses generic.status. */
    return instance->generic.status == 0;
}

void subghz_protocol_renault_v1_bf_init(RenaultV1BfContext* ctx, void* decoder_context) {
    furi_assert(ctx);
    furi_assert(decoder_context);
    SubGhzProtocolDecoderRenaultV1* instance = decoder_context;
    memset(ctx, 0, sizeof(RenaultV1BfContext));
    /* Same 11 bytes the decoder verified -- the cipher brutes this payload. */
    renault_v1_pack_payload(instance->generic.data, instance->generic.data_2, ctx->payload);
    ctx->total = RENAULT_V1_BF_RANGE;
    ctx->state = RenaultV1BfStateIdle;
}

void subghz_protocol_renault_v1_bf_apply(RenaultV1BfContext* ctx, void* decoder_context) {
    furi_assert(ctx);
    furi_assert(decoder_context);
    if(ctx->state != RenaultV1BfStateFound) return;
    SubGhzProtocolDecoderRenaultV1* instance = decoder_context;
    /* Store the recovered IV in generic.seed; button + counter are slices of it (carved by
     * check_remote_controller), so nothing else is stored. Set generic.status to mark it recovered --
     * the marker gates recovery, not the Seed value (see needs_bruteforce). */
    instance->generic.seed = renault_v1_iv_to_u32(ctx->iv);
    instance->generic.status = 1;
}

void subghz_protocol_renault_v1_bf_set_not_found(void* decoder_context) {
    furi_assert(decoder_context);
    SubGhzProtocolDecoderBase* base = decoder_context;
    if(base->protocol != &subghz_protocol_renault_v1) return;
    SubGhzProtocolDecoderRenaultV1* instance = decoder_context;
    instance->generic.status = 2; // brute exhausted, key not found (session-only, never serialized)
}
