#include "gm_rke.h"

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include "../encode_plugin.h"

/*
 * GM ABO1502T — General Motors car remote (RKE) decoder.
 *
 * Ported from the rtl_433 device "GM ABO1502T Car Remote" (gm_car_remote.c,
 * (C) 2024 Ethan Halsall). rtl_433 works on a pre-demodulated bitstream; here
 * we re-implement the same OOK-PPM demodulation as a pulse-duration state
 * machine over LevelDuration events, since the Flipper subghz worker hands us
 * one (level, duration) edge at a time and there is no ready-made bitbuffer.
 *
 * Physical layer (from rtl_433 r_device):
 *   - Carrier ~314.9 MHz, OOK (AM), band mapped to 315 flag here
 *   - Modulation OOK_PULSE_PPM: each pulse (mark) is a fixed short width and
 *     the GAP (space) after it carries the bit:
 *         short gap ~300us  -> bit 0
 *         long  gap ~500us  -> bit 1
 *   - reset_limit 20000us ends a row
 *
 * Frame (rtl_433 gm_car_remote_decode):
 *   - A long wake-up preamble precedes the payload and may be truncated, so
 *     rtl_433 aligns to the END of the row: offset = total_bits - 113, then
 *     extracts 112 bits (14 bytes). We mirror that: collect all PPM bits until
 *     a terminating gap, require >= 113 collected bits, and slice the 14-byte
 *     window starting at (count - 113), taking 112 bits (dropping the final
 *     bit exactly as rtl_433 does).
 *
 * Byte layout of the 14-byte (112-bit) window:
 *     PP xxxx cccc IIIIIIII SSSSSS EEEEEE CC
 *   byte[0]      : 0xFF wake-up validation (must equal 0xFF)
 *   byte[1]      : 8-bit unknown prefix (possibly high byte of ID)
 *   byte[2]      : upper nibble = button checksum, low 3 bits = button code
 *   byte[3..6]   : 32-bit ID (big-endian)
 *   byte[7..9]   : 24-bit sequence (rolling counter, unencrypted)
 *   byte[10..12] : 24-bit encrypted payload
 *   byte[13]     : 8-bit checksum of the whole payload
 *
 * Integrity (rejects false positives — this is the whole point of the gate):
 *   - button checksum: add_nibbles(bytes+2, 1): (byte[2]>>4)+(byte[2]&0xF);
 *     valid iff nonzero AND low nibble == 0.
 *   - full checksum: add_bytes(bytes+1, 13): sum of byte[1..13];
 *     valid iff nonzero AND low byte == 0.
 *
 * No TX: encoder is a stub (rolling code / encrypted field, not replayable).
 */

#define TAG "GmRke"

/* PPM timing (microseconds). Space (gap) after each pulse carries the bit. */
#define GM_RKE_TE_SHORT      300   /* short gap -> bit 0 */
#define GM_RKE_TE_LONG       500   /* long gap  -> bit 1 */
#define GM_RKE_SPACE_MIN     120   /* below this a gap is noise */
#define GM_RKE_BIT_THRESHOLD 400   /* midpoint: gap >= this -> 1, else 0 */
#define GM_RKE_SPACE_MAX     900   /* above this a gap terminates the frame */
#define GM_RKE_MARK_MIN      100   /* plausible pulse (mark) width lower bound */
#define GM_RKE_MARK_MAX      1000  /* plausible pulse (mark) width upper bound */

#define GM_RKE_FRAME_BITS 113      /* rtl_433: offset = total_bits - 113 */
#define GM_RKE_PAYLOAD_BITS 112    /* 14 bytes extracted */
#define GM_RKE_PAYLOAD_BYTES 14
#define GM_RKE_MIN_BITS 112        /* min_count_bit_for_found reported */

/* Rolling bit buffer. Wake-up preamble can be long; we only ever need the last
 * GM_RKE_FRAME_BITS, so cap the buffer and slide it down on overflow. */
#define GM_RKE_MAX_BITS  256
#define GM_RKE_KEEP_BITS 160       /* retained after an overflow slide */

static const SubGhzBlockConst subghz_protocol_gm_rke_const = {
    .te_short = GM_RKE_TE_SHORT,
    .te_long = GM_RKE_TE_LONG,
    .te_delta = 150,
    .min_count_bit_for_found = GM_RKE_MIN_BITS,
};

typedef enum {
    GmRkeDecoderStepReset = 0,
    GmRkeDecoderStepData,
} GmRkeDecoderStep;

struct SubGhzProtocolDecoderGmRke {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t bit_count;
    uint8_t bits[GM_RKE_MAX_BITS]; /* one 0/1 per PPM symbol, MSB-first order */

    /* Live-only extras (not serialized; recovered on decode, 0 after load). */
    uint8_t unknown_prefix; /* byte[1] */
    uint32_t encrypted;     /* byte[10..12], 24-bit */
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderGmRke);


struct SubGhzProtocolEncoderGmRke {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderGmRke);


// ============================================================================
// Protocol registration
// ============================================================================

const SubGhzProtocolDecoder subghz_protocol_gm_rke_decoder = {
    .alloc = subghz_protocol_decoder_gm_rke_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_gm_rke_feed,
    .reset = subghz_protocol_decoder_gm_rke_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_gm_rke_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_gm_rke_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_gm_rke");

const SubGhzProtocol subghz_protocol_gm_rke = {
    .name = GM_RKE_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_gm_rke_decoder,
    .encoder = &subghz_protocol_gm_rke_encoder_ext.vtable,
};

// ============================================================================
// Field derivation (works both live and after load, from generic.data)
//
// Packed layout of generic.data (59 bits used):
//   [58:27] 32-bit ID   [26:3] 24-bit sequence   [2:0] 3-bit button
// ============================================================================

// ============================================================================
// Frame processing: slice window, validate checksums, emit
// ============================================================================

static void subghz_protocol_gm_rke_process_frame(SubGhzProtocolDecoderGmRke* instance) {
    if(instance->bit_count < GM_RKE_FRAME_BITS) return;

    /* Align to end of collected bits: window = bits[count-113 .. count-2],
     * i.e. 112 bits (14 bytes) starting at (count-113). rtl_433 drops the
     * final collected bit (index count-1). */
    uint16_t off = instance->bit_count - GM_RKE_FRAME_BITS;

    uint8_t bytes[GM_RKE_PAYLOAD_BYTES];
    for(uint16_t i = 0; i < GM_RKE_PAYLOAD_BYTES; i++) {
        uint8_t b = 0;
        for(uint16_t j = 0; j < 8; j++) {
            b = (uint8_t)((b << 1) | (instance->bits[off + i * 8 + j] & 1));
        }
        bytes[i] = b;
    }

    /* Wake-up validation byte. */
    if(bytes[0] != 0xFF) return;

    /* Button checksum: add_nibbles(bytes+2, 1). */
    int button_checksum = (bytes[2] >> 4) + (bytes[2] & 0x0F);
    if(button_checksum == 0 || (button_checksum & 0x0F) != 0) return;

    /* Full checksum: add_bytes(bytes+1, 13). */
    int full_checksum = 0;
    for(uint16_t i = 1; i < GM_RKE_PAYLOAD_BYTES; i++) {
        full_checksum += bytes[i];
    }
    if(full_checksum == 0 || (full_checksum & 0xFF) != 0) return;

    /* Parse fields (matches rtl_433). */
    uint8_t button = bytes[2] & 0x7;
    uint32_t id = ((uint32_t)bytes[3] << 24) | ((uint32_t)bytes[4] << 16) |
                  ((uint32_t)bytes[5] << 8) | (uint32_t)bytes[6];
    uint32_t sequence = ((uint32_t)bytes[7] << 16) | ((uint32_t)bytes[8] << 8) | (uint32_t)bytes[9];
    uint32_t encrypted =
        ((uint32_t)bytes[10] << 16) | ((uint32_t)bytes[11] << 8) | (uint32_t)bytes[12];

    /* Pack id/sequence/button into generic.data so serialize/load round-trip. */
    uint64_t packed = ((uint64_t)id << 27) | ((uint64_t)(sequence & 0xFFFFFF) << 3) |
                      (uint64_t)(button & 0x7);

    instance->generic.data = packed;
    instance->generic.data_count_bit = GM_RKE_PAYLOAD_BITS;
    instance->generic.serial = id;
    instance->generic.btn = button;
    instance->generic.cnt = sequence;
    instance->unknown_prefix = bytes[1];
    instance->encrypted = encrypted;

    /* Feed the hash block a meaningful value. */
    instance->decoder.decode_data = packed;
    instance->decoder.decode_count_bit = GM_RKE_PAYLOAD_BITS;

    FURI_LOG_D(
        TAG,
        "ID:%02X%08lX Btn:%X Seq:%06lX",
        bytes[1],
        (unsigned long)id,
        button,
        (unsigned long)sequence);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

// ============================================================================
// Decoder: alloc / free / reset
// ============================================================================

void* subghz_protocol_decoder_gm_rke_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderGmRke* instance = malloc(sizeof(SubGhzProtocolDecoderGmRke));
    instance->base.protocol = &subghz_protocol_gm_rke;
    instance->generic.protocol_name = subghz_protocol_gm_rke.name;
    return instance;
}



void subghz_protocol_decoder_gm_rke_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderGmRke* instance = context;
    instance->decoder.parser_step = GmRkeDecoderStepReset;
    instance->bit_count = 0;
}

// ============================================================================
// Decoder: feed
//
// OOK-PPM: each pulse (mark, level=true) is fixed width; the following gap
// (space, level=false) carries one bit. short gap -> 0, long gap -> 1. A gap
// longer than GM_RKE_SPACE_MAX terminates the frame; we then attempt to decode
// the accumulated bits (aligned to the last 113).
// ============================================================================

static void subghz_protocol_gm_rke_add_bit(SubGhzProtocolDecoderGmRke* instance, uint8_t bit) {
    if(instance->bit_count >= GM_RKE_MAX_BITS) {
        /* Slide window down, keep the most recent GM_RKE_KEEP_BITS. */
        uint16_t drop = instance->bit_count - GM_RKE_KEEP_BITS;
        for(uint16_t i = 0; i < GM_RKE_KEEP_BITS; i++) {
            instance->bits[i] = instance->bits[drop + i];
        }
        instance->bit_count = GM_RKE_KEEP_BITS;
    }
    instance->bits[instance->bit_count++] = bit & 1;
}

void subghz_protocol_decoder_gm_rke_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderGmRke* instance = context;

    switch(instance->decoder.parser_step) {
    case GmRkeDecoderStepReset:
        /* Wait for a plausible pulse (mark) to begin a frame. */
        if(level && duration >= GM_RKE_MARK_MIN && duration <= GM_RKE_MARK_MAX) {
            instance->bit_count = 0;
            instance->decoder.parser_step = GmRkeDecoderStepData;
        }
        break;

    case GmRkeDecoderStepData:
        if(level) {
            /* Mark: must be a plausible short pulse; otherwise the burst is
             * over (long carrier / noise) -> try to decode, then reset. */
            if(duration < GM_RKE_MARK_MIN || duration > GM_RKE_MARK_MAX) {
                subghz_protocol_gm_rke_process_frame(instance);
                instance->bit_count = 0;
                instance->decoder.parser_step = GmRkeDecoderStepReset;
            }
        } else {
            /* Space: carries the bit, or terminates the frame. */
            if(duration > GM_RKE_SPACE_MAX) {
                subghz_protocol_gm_rke_process_frame(instance);
                instance->bit_count = 0;
                instance->decoder.parser_step = GmRkeDecoderStepReset;
            } else if(duration >= GM_RKE_SPACE_MIN) {
                uint8_t bit = (duration >= GM_RKE_BIT_THRESHOLD) ? 1 : 0;
                subghz_protocol_gm_rke_add_bit(instance, bit);
            } else {
                /* Runt gap: invalid symbol -> abandon this burst. */
                subghz_protocol_gm_rke_process_frame(instance);
                instance->bit_count = 0;
                instance->decoder.parser_step = GmRkeDecoderStepReset;
            }
        }
        break;
    }
}

// ============================================================================
// Decoder: hash / serialize / deserialize
// ============================================================================





SubGhzProtocolStatus
    subghz_protocol_decoder_gm_rke_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderGmRke* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_gm_rke_const.min_count_bit_for_found);
}

// ============================================================================
// Decoder: get_string
// ============================================================================

