#include "alps_rke.h"

/* ==== Self-contained sub-GHz helpers (de-refactored from the ProtoPirate/applab
 * "protocols_common" pp-framework) so this decoder depends only on the core
 * sub-GHz lib and carries no shared helper unit into lib/. All helpers are
 * static inline: unused ones are dropped with no -Wunused-function warning, and
 * the TX scratch buffer is a function-local static (no file-scope unused-var).
 * FoxFW2.0 2026-10-18 internal batch 2. ==== */
#include <furi.h>
#include <string.h>
#include <lib/subghz/types.h>
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>
#include <flipper_format/flipper_format.h>

#ifndef ENABLE_EMULATE_FEATURE
#define ENABLE_EMULATE_FEATURE 1
#endif

#ifndef PP_COMPAT_SHIM_DEFINED
#define PP_COMPAT_SHIM_DEFINED
#define FF_BIT                    "Bit"
#define FF_KEY                    "Key"
#define FF_SERIAL                 "Serial"
#define FF_BTN                    "Btn"
#define FF_CNT                    "Cnt"
#define FF_REPEAT                 "Repeat"
#define FF_PROTOCOL               "Protocol"
#define FF_TYPE                   "Type"
#define PP_FIELD_SERIAL           0x01U
#define PP_FIELD_BTN              0x02U
#define PP_FIELD_CNT              0x04U
#define PP_FIELD_TYPE             0x08U
#define PP_ENCODER_REPEAT_MAX     50U
#define PP_SHARED_UPLOAD_CAPACITY 2048U

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
} ProtoPirateDecoderHeader;

typedef struct {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
} ProtoPirateEncoderHeader;

#endif /* PP_COMPAT_SHIM_DEFINED */



#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#include <string.h>
#include "common.h"
#include "../encode_plugin.h"
#include "pp_auto_common.h"

/*
 * Alps FWB1U545 — car RKE keyfob decoder
 *
 * Ported from the rtl_433 device `alps_fwb1u545.c`.
 *
 * Physical layer (from rtl_433 r_device):
 *   - Modulation : FSK_PULSE_MANCHESTER_ZEROBIT
 *   - short_width: 500us   (Manchester half-bit)  -> te_short = 500, te_long = 1000
 *   - reset_limit: 1500us  (end-of-frame gap)
 *   - Carrier    : 433.92 MHz SRD (typical for these fobs; not stated in rtl_433)
 *
 * Frame: 76 Manchester bits (9 full bytes + a trailing 4-bit nibble):
 *   byte[0]      = 0x55                    preamble / sync
 *   byte[1..4]   = ID (32 bit, big-endian) serial
 *   byte[5] >> 4 = button (4 bit)
 *   byte[5]      = byte[6]                 button byte is duplicated (sanity check)
 *   byte[7..8]   = sequence (16 bit rolling counter)
 *   byte[9] >> 4 = 4 unknown bits (unused)
 *
 * rtl_433 has no CRC; it validates with:
 *   byte[0] == 0x55 && byte[5] == byte[6] && id != 0 && id != 0xffffffff
 * We keep exactly that gate (plus rejecting an all-preamble 0x55.../0xAA... id)
 * to avoid the false-positives this codebase has fought with fuzzy car fobs.
 *
 * feed() re-implements the rtl_433 pulse-slicer as a LevelDuration state
 * machine: the 0x55 preamble is an alternating-bit run (a train of te_short
 * pulses), so we lock on a run of shorts, Manchester-decode the stream that
 * follows into a bit buffer, then slide a window to find the 0x55 sync byte and
 * validate the 76-bit frame. Polarity is recovered automatically by retrying
 * with an inverted bitstream (covers the ZEROBIT convention either way).
 */

#define TAG "AlpsRke"

#define ALPS_RKE_FRAME_BITS   76 /* full Manchester frame length */
#define ALPS_RKE_NEED_BITS    72 /* bits we actually parse: byte[0..8] */
#define ALPS_RKE_MIN_PREAMBLE 8 /* consecutive te_short pulses to lock preamble */
#define ALPS_RKE_MAX_BITS     96 /* decoded-bit ring capacity */
#define ALPS_RKE_RING_DROP    16 /* bits dropped from the front when ring fills */

static const SubGhzBlockConst subghz_protocol_alps_rke_const = {
    .te_short = 500,
    .te_long = 1000,
    .te_delta = 150,
    .min_count_bit_for_found = ALPS_RKE_FRAME_BITS,
};

typedef enum {
    AlpsRkeDecoderStepReset = 0,
    AlpsRkeDecoderStepPreamble,
    AlpsRkeDecoderStepData,
} AlpsRkeDecoderStep;

struct SubGhzProtocolDecoderAlpsRke {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t preamble_count;
    uint16_t bit_count;
    ManchesterState manchester_state;
    uint8_t bits[ALPS_RKE_MAX_BITS];
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderAlpsRke);


struct SubGhzProtocolEncoderAlpsRke {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderAlpsRke);


// ============================================================================
// Protocol registration
// ============================================================================

const SubGhzProtocolDecoder subghz_protocol_alps_rke_decoder = {
    .alloc = subghz_protocol_decoder_alps_rke_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_alps_rke_feed,
    .reset = subghz_protocol_decoder_alps_rke_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_alps_rke_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_alps_rke_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_alps_rke");

const SubGhzProtocol subghz_protocol_alps_rke = {
    .name = ALPS_RKE_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_alps_rke_decoder,
    .encoder = &subghz_protocol_alps_rke_encoder_ext.vtable,
};

// ============================================================================
// Decoder: alloc / free / reset
// ============================================================================

void* subghz_protocol_decoder_alps_rke_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderAlpsRke* instance = malloc(sizeof(SubGhzProtocolDecoderAlpsRke));
    instance->base.protocol = &subghz_protocol_alps_rke;
    instance->generic.protocol_name = subghz_protocol_alps_rke.name;
    return instance;
}



void subghz_protocol_decoder_alps_rke_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderAlpsRke* instance = context;
    instance->decoder.parser_step = AlpsRkeDecoderStepReset;
    instance->preamble_count = 0;
    instance->bit_count = 0;
    instance->manchester_state = ManchesterStateMid1;
}

// ============================================================================
// Frame parsing: slide a window over the decoded bits, find the 0x55 sync
// byte and validate the rtl_433 field layout.
// ============================================================================

static bool subghz_protocol_alps_rke_try_decode(SubGhzProtocolDecoderAlpsRke* instance, bool invert) {
    const uint16_t count = instance->bit_count;
    if(count < ALPS_RKE_NEED_BITS) return false;

    int best_off = -1;
    uint8_t best_bytes[9] = {0};

    for(int off = 0; off + ALPS_RKE_NEED_BITS <= (int)count; off++) {
        uint8_t bytes[9];
        for(int k = 0; k < 9; k++) {
            uint8_t b = 0;
            for(int j = 0; j < 8; j++) {
                uint8_t bit = instance->bits[off + (k * 8) + j];
                if(invert) bit ^= 1u;
                b = (uint8_t)((b << 1) | (bit & 1u));
            }
            bytes[k] = b;
        }

        /* rtl_433 sanity: preamble byte and duplicated button byte */
        if(bytes[0] != 0x55) continue;
        if(bytes[5] != bytes[6]) continue;

        uint32_t id = ((uint32_t)bytes[1] << 24) | ((uint32_t)bytes[2] << 16) |
                      ((uint32_t)bytes[3] << 8) | (uint32_t)bytes[4];
        if(id == 0 || id == 0xFFFFFFFF) continue;
        /* Reject a window that sits entirely inside the alternating preamble
         * (which decodes to 0x55.../0xAA...) and would masquerade as a frame. */
        if(id == 0x55555555 || id == 0xAAAAAAAA) continue;

        /* Keep the last valid alignment: the real 0x55 sits at the tail of the
         * preamble run, immediately before the ID. */
        best_off = off;
        memcpy(best_bytes, bytes, sizeof(bytes));
    }

    if(best_off < 0) return false;

    uint32_t id = ((uint32_t)best_bytes[1] << 24) | ((uint32_t)best_bytes[2] << 16) |
                  ((uint32_t)best_bytes[3] << 8) | (uint32_t)best_bytes[4];
    uint8_t button = best_bytes[5] >> 4;
    uint16_t sequence = (uint16_t)(((uint16_t)best_bytes[7] << 8) | best_bytes[8]);

    /* Pack the unique payload (ID + button bytes + sequence) into 64 bits. */
    uint64_t data = ((uint64_t)id << 32) | ((uint64_t)best_bytes[5] << 24) |
                    ((uint64_t)best_bytes[6] << 16) | (uint64_t)sequence;

    instance->generic.data = data;
    instance->generic.data_count_bit = ALPS_RKE_FRAME_BITS;
    instance->generic.serial = id;
    instance->generic.btn = button;
    instance->generic.cnt = sequence;

    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = ALPS_RKE_FRAME_BITS;

    FURI_LOG_D(TAG, "Frame: Id:%08lX Btn:%X Cnt:%04X", (unsigned long)id, button, sequence);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
    return true;
}

static void subghz_protocol_alps_rke_finalize(SubGhzProtocolDecoderAlpsRke* instance) {
    if(!subghz_protocol_alps_rke_try_decode(instance, false)) {
        subghz_protocol_alps_rke_try_decode(instance, true);
    }
    instance->decoder.parser_step = AlpsRkeDecoderStepReset;
    instance->preamble_count = 0;
    instance->bit_count = 0;
}

static void subghz_protocol_alps_rke_store_bit(SubGhzProtocolDecoderAlpsRke* instance, bool bit) {
    if(instance->bit_count >= ALPS_RKE_MAX_BITS) {
        /* Ring down: keep the most recent bits (the real frame is nearest the
         * end-of-frame gap), drop the oldest preamble bits. */
        memmove(
            instance->bits,
            instance->bits + ALPS_RKE_RING_DROP,
            ALPS_RKE_MAX_BITS - ALPS_RKE_RING_DROP);
        instance->bit_count = ALPS_RKE_MAX_BITS - ALPS_RKE_RING_DROP;
    }
    instance->bits[instance->bit_count++] = bit ? 1u : 0u;
}

static void subghz_protocol_alps_rke_manchester_feed(
    SubGhzProtocolDecoderAlpsRke* instance,
    bool level,
    uint32_t duration) {
    ManchesterEvent event =
        ppa_manchester_event(duration, level, &subghz_protocol_alps_rke_const);
    if(event == ManchesterEventReset) {
        /* Bad timing / end-of-frame gap: try to decode what we have. */
        subghz_protocol_alps_rke_finalize(instance);
        return;
    }

    bool data_bit = false;
    if(manchester_advance(
           instance->manchester_state, event, &instance->manchester_state, &data_bit)) {
        subghz_protocol_alps_rke_store_bit(instance, data_bit);
    }
}

// ============================================================================
// Decoder: feed
// ============================================================================

void subghz_protocol_decoder_alps_rke_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderAlpsRke* instance = context;

    const SubGhzBlockConst* t = &subghz_protocol_alps_rke_const;
    bool is_short = DURATION_DIFF(duration, t->te_short) < t->te_delta;

    switch(instance->decoder.parser_step) {
    case AlpsRkeDecoderStepReset:
        if(is_short) {
            instance->preamble_count = 1;
            instance->decoder.parser_step = AlpsRkeDecoderStepPreamble;
        }
        break;

    case AlpsRkeDecoderStepPreamble:
        if(is_short) {
            instance->preamble_count++;
            if(instance->preamble_count >= ALPS_RKE_MIN_PREAMBLE) {
                /* Locked on the alternating-bit (0x55) preamble train. Start
                 * Manchester-decoding from here so the ID is captured in full;
                 * the 0x55 sync byte is recovered later by the window search. */
                instance->decoder.parser_step = AlpsRkeDecoderStepData;
                instance->bit_count = 0;
                instance->manchester_state = ManchesterStateMid1;
                manchester_advance(
                    instance->manchester_state,
                    ManchesterEventReset,
                    &instance->manchester_state,
                    NULL);
                subghz_protocol_alps_rke_manchester_feed(instance, level, duration);
            }
        } else {
            /* Preamble broken before lock -> noise. */
            instance->decoder.parser_step = AlpsRkeDecoderStepReset;
            instance->preamble_count = 0;
        }
        break;

    case AlpsRkeDecoderStepData:
        subghz_protocol_alps_rke_manchester_feed(instance, level, duration);
        break;
    }
}

// ============================================================================
// Decoder: hash / serialize / deserialize / string
// ============================================================================





SubGhzProtocolStatus
    subghz_protocol_decoder_alps_rke_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderAlpsRke* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_alps_rke_const.min_count_bit_for_found);
}

