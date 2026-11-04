#include "nidec_rke.h"

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include "../encode_plugin.h"

/*
 * Nidec RKE — Nidec car remote (Honda OEM OUCG8D-344H-A), ported from the
 * rtl_433 device decoder `nidec_car_remote.c` (© 2024 Ethan Halsall, GPL).
 *
 * RF profile (from rtl_433):
 *   - Carrier   : 313.8 MHz
 *   - Modulation: FSK, pulse-width modulated (FSK_PULSE_PWM)
 *   - Bit '1'   : short pulse ~250us
 *   - Bit '0'   : long  pulse ~500us
 *   - Reset gap : ~1000us (end of packet)
 *
 * Framing (rtl_433 searches the last 32 bits of the preamble = 0xFFFFFFF0,
 * i.e. a long run of '1' bits terminated by four '0' bits, then 64 payload
 * bits follow). The RF bits are INVERTED relative to the reported bytes, so
 * every captured payload bit is complemented before assembly.
 *
 * Payload (8 bytes, MSB-first, AFTER inversion):
 *     SSSS IIIIII 5b CCCC
 *   - S : bytes[0..1]  16-bit sequence (plain rolling counter)
 *   - I : bytes[2..4]  24-bit remote ID
 *   - 5 : bytes[5]>>4  constant 0x5 (present on every real remote)
 *   - b : bytes[5]&0xF 4-bit button code
 *   - C : bytes[6..7]  16-bit keyed security/rolling-code authenticator
 *
 * The security field is NOT a conventional checksum — rtl_433's author
 * exhaustively tested every CRC-8/16 polynomial, reflection, init/final and
 * byte subset plus additive/XOR/Fletcher variants and found no match; it is a
 * keyed authenticator computed inside the transmitter MCU and cannot be
 * verified from the wire. There is therefore no CRC to gate on. Instead we
 * apply the exact structural sanity gate rtl_433 uses (constant 0x5 nibble,
 * non-degenerate id/sequence/security, and a button whitelist) to reject
 * false positives — this codebase has previously fought exactly that problem.
 *
 * On real captures the trailing 16-bit security field is frequently truncated
 * by the demod, so we accept 56..64 payload bits, left-align what we captured,
 * zero-pad the tail (matching rtl_433's bitbuffer_extract_bytes behaviour) and
 * record how many security bits were genuinely observed.
 */

#define TAG "NidecRke"

/* PWM pulse (mark) widths, microseconds */
#define NIDEC_TE_SHORT 250 /* bit '1' */
#define NIDEC_TE_LONG  500 /* bit '0' */

/* Classification bounds (~+/-30% tolerance, non-overlapping) */
#define NIDEC_MARK_MIN     130  /* below this a mark is noise */
#define NIDEC_MARK_MID     375  /* short/long split point */
#define NIDEC_MARK_MAX     700  /* above this a mark is noise */
#define NIDEC_GAP_RESET    700  /* space >= this ends the packet */

/* Sync: last 32 preamble bits, non-inverted */
#define NIDEC_SYNC_WORD    0xFFFFFFF0UL

/* Payload sizing */
#define NIDEC_PAYLOAD_BITS 64 /* full frame */
#define NIDEC_MIN_BITS     56 /* rtl_433 requires >=56 bits after the sync */
#define NIDEC_HEAD_BITS    48 /* sequence+id+byte5 always fully captured */

static const SubGhzBlockConst subghz_protocol_nidec_rke_const = {
    .te_short = NIDEC_TE_SHORT,
    .te_long = NIDEC_TE_LONG,
    .te_delta = 120,
    .min_count_bit_for_found = NIDEC_MIN_BITS,
};

// ============================================================================
// Instance / state
// ============================================================================

typedef enum {
    NidecRkeDecoderStepReset = 0, /* hunting for the 0xFFFFFFF0 sync */
    NidecRkeDecoderStepData, /* collecting payload bits */
} NidecRkeDecoderStep;

struct SubGhzProtocolDecoderNidecRke {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t sync_reg; /* rolling 32-bit window of raw (non-inverted) bits */
    uint64_t payload; /* inverted payload bits, MSB-first */
    uint16_t payload_bits; /* count collected so far */
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderNidecRke);


struct SubGhzProtocolEncoderNidecRke {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderNidecRke);


// ============================================================================
// Protocol registration
// ============================================================================

const SubGhzProtocolDecoder subghz_protocol_nidec_rke_decoder = {
    .alloc = subghz_protocol_decoder_nidec_rke_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_nidec_rke_feed,
    .reset = subghz_protocol_decoder_nidec_rke_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_nidec_rke_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_nidec_rke_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_nidec_rke");

const SubGhzProtocol subghz_protocol_nidec_rke = {
    .name = NIDEC_RKE_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_nidec_rke_decoder,
    .encoder = &subghz_protocol_nidec_rke_encoder_ext.vtable,
};

// ============================================================================
// Field extraction helpers (work off the left-aligned inverted payload)
// ============================================================================

static inline uint8_t nidec_byte(uint64_t data, uint8_t index) {
    /* byte 0 = most significant, matching rtl_433 bytes[0..7] */
    return (uint8_t)((data >> (56 - 8 * index)) & 0xFF);
}

static bool nidec_button_is_valid(uint8_t button) {
    /* only these 5 codes have ever been observed on real remotes */
    return button == 0x3 || button == 0x4 || button == 0x5 || button == 0x6 || button == 0xF;
}

// ============================================================================
// Process a completed frame
// ============================================================================

static void subghz_protocol_nidec_rke_process(SubGhzProtocolDecoderNidecRke* instance) {
    uint16_t bits = instance->payload_bits;
    if(bits < NIDEC_MIN_BITS) return;
    if(bits > NIDEC_PAYLOAD_BITS) bits = NIDEC_PAYLOAD_BITS;

    /* Left-align captured (already-inverted) bits; zero-pad the truncated tail
     * exactly as rtl_433's bitbuffer_extract_bytes() does. */
    uint64_t data = instance->payload;
    if(bits < NIDEC_PAYLOAD_BITS) data <<= (NIDEC_PAYLOAD_BITS - bits);

    uint8_t byte5 = nidec_byte(data, 5);

    /* Structural sanity gate (there is no verifiable checksum) */
    if((byte5 & 0xF0) != 0x50) return;

    uint32_t sequence = ((uint32_t)nidec_byte(data, 0) << 8) | nidec_byte(data, 1);
    uint32_t id = ((uint32_t)nidec_byte(data, 2) << 16) | ((uint32_t)nidec_byte(data, 3) << 8) |
                  nidec_byte(data, 4);
    uint8_t button = byte5 & 0x0F;
    uint32_t security = ((uint32_t)nidec_byte(data, 6) << 8) | nidec_byte(data, 7);

    if(id == 0 || id == 0xFFFFFF) return;
    if(sequence == 0 || sequence == 0xFFFF) return;
    if(security == 0 || security == 0xFFFF) return;
    if(!nidec_button_is_valid(button)) return;

    /* Recovered fields into generic (data_count_bit carries the real captured
     * width so security_bits survives save/load). */
    instance->generic.data = data;
    instance->generic.data_count_bit = bits;
    instance->generic.serial = id;
    instance->generic.btn = button;
    instance->generic.cnt = sequence;
    instance->generic.seed = security;

    /* Feed the block hash pipeline */
    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = bits;

    FURI_LOG_D(
        TAG, "ID:%06lX Seq:%04lX Btn:%X Sec:%04lX (%dbit)", id, sequence, button, security, bits);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

// ============================================================================
// Decoder: alloc / free / reset
// ============================================================================

void* subghz_protocol_decoder_nidec_rke_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderNidecRke* instance = malloc(sizeof(SubGhzProtocolDecoderNidecRke));
    instance->base.protocol = &subghz_protocol_nidec_rke;
    instance->generic.protocol_name = subghz_protocol_nidec_rke.name;
    return instance;
}



void subghz_protocol_decoder_nidec_rke_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderNidecRke* instance = context;
    instance->decoder.parser_step = NidecRkeDecoderStepReset;
    instance->sync_reg = 0;
    instance->payload = 0;
    instance->payload_bits = 0;
}

// ============================================================================
// Decoder: feed — PWM pulse slicer + rolling sync detector
//
// A pulse (level==true) carries the bit: short ~250us => '1', long ~500us =>
// '0'. The intervening spaces only separate bits; a space >= NIDEC_GAP_RESET
// terminates the packet. Raw bits are complemented on capture because the RF
// bytes are inverted relative to the reported payload.
// ============================================================================

void subghz_protocol_decoder_nidec_rke_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderNidecRke* instance = context;

    if(!level) {
        /* Space: only a long gap is significant (packet boundary). */
        if(duration >= NIDEC_GAP_RESET) {
            if(instance->decoder.parser_step == NidecRkeDecoderStepData) {
                subghz_protocol_nidec_rke_process(instance);
            }
            instance->decoder.parser_step = NidecRkeDecoderStepReset;
            instance->sync_reg = 0;
            instance->payload = 0;
            instance->payload_bits = 0;
        }
        return;
    }

    /* Pulse: classify by width into a raw bit (short=1, long=0). */
    uint8_t raw_bit;
    if(duration >= NIDEC_MARK_MIN && duration < NIDEC_MARK_MID) {
        raw_bit = 1;
    } else if(duration >= NIDEC_MARK_MID && duration <= NIDEC_MARK_MAX) {
        raw_bit = 0;
    } else {
        /* Out-of-range pulse: abandon any in-progress frame. */
        instance->decoder.parser_step = NidecRkeDecoderStepReset;
        instance->sync_reg = 0;
        instance->payload = 0;
        instance->payload_bits = 0;
        return;
    }

    switch(instance->decoder.parser_step) {
    case NidecRkeDecoderStepReset:
        instance->sync_reg = (instance->sync_reg << 1) | raw_bit;
        if(instance->sync_reg == NIDEC_SYNC_WORD) {
            instance->decoder.parser_step = NidecRkeDecoderStepData;
            instance->payload = 0;
            instance->payload_bits = 0;
        }
        break;

    case NidecRkeDecoderStepData:
        /* Invert on capture (RF bytes are inverted). */
        instance->payload = (instance->payload << 1) | (uint8_t)(raw_bit ^ 1);
        instance->payload_bits++;
        if(instance->payload_bits >= NIDEC_PAYLOAD_BITS) {
            subghz_protocol_nidec_rke_process(instance);
            instance->decoder.parser_step = NidecRkeDecoderStepReset;
            instance->sync_reg = 0;
            instance->payload = 0;
            instance->payload_bits = 0;
        }
        break;
    }
}

// ============================================================================
// Decoder: hash / serialize / deserialize
// ============================================================================





SubGhzProtocolStatus
    subghz_protocol_decoder_nidec_rke_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderNidecRke* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_nidec_rke_const.min_count_bit_for_found);
}

// ============================================================================
// Decoder: get_string — derive fields from generic.data so loaded files render
// ============================================================================

