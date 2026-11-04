#include "siemens_vdo.h"
#include "pp_auto_common.h"
#include <lib/toolbox/manchester_decoder.h>
#include "common.h"

/*
 * Siemens VDO car remote (a.k.a. Siemens 5WY72XX) decoder.
 *
 * Ported from the rtl_433 device driver
 *   src/devices/siemens_5wy72xx.c (merbanan/rtl_433)
 *
 * RF profile:
 *   - Carrier: 315.1 MHz (also seen on 433.92 MHz in EU variants), OOK / AM.
 *   - Modulation: OOK_PULSE_MANCHESTER_ZEROBIT, half-bit ~220us
 *     (=> full Manchester bit ~440us). rtl_433 reset_limit 10000us.
 *   - After Manchester demod the stream carries a 16-bit sync word 0x6001
 *     followed immediately by a 96-bit (12 byte) little-endian payload.
 *
 * Frame layout (12 payload bytes, byte index after the 0x6001 sync):
 *   bytes[0..3]  : ID / serial            (little-endian, printed 3 2 1 0)
 *   bytes[4]     : button bitmask
 *   bytes[5..6]  : sequence / rolling counter (big-endian: b5<<8 | b6)
 *   bytes[7..10] : encrypted payload      (little-endian, printed 10 9 8 7)
 *   bytes[11]    : XOR check byte
 *
 * Integrity: XOR of all 12 payload bytes must be 0 (the check byte makes the
 * running XOR close to zero). A sanity gate rejects all-0x00 / all-0xFF frames.
 * Both checks are enforced before a frame is emitted, which suppresses the
 * false-positive detections this codebase has historically fought.
 *
 * Manchester polarity: the Flipper toolbox decoder may recover this signal with
 * inverted bit polarity relative to rtl_433. We therefore also accept the
 * inverted sync word (~0x6001 = 0x9FFE); when matched we bit-invert the
 * captured bytes before parsing. XOR==0 is preserved under a full 12-byte
 * inversion (even byte count), so the check still gates correctly.
 *
 * Encoder: rolling / encrypted code (not fixed) -> TX not implemented (stub).
 */

#define TAG "SiemensVDO"

#define SIEMENS_VDO_SYNC     0x6001U
#define SIEMENS_VDO_SYNC_INV 0x9FFEU
#define SIEMENS_VDO_PAYLOAD_BITS  96U
#define SIEMENS_VDO_PAYLOAD_BYTES 12U

static const SubGhzBlockConst subghz_protocol_siemens_vdo_const = {
    .te_short = 220,
    .te_long = 440,
    .te_delta = 100,
    .min_count_bit_for_found = SIEMENS_VDO_PAYLOAD_BITS,
};

struct SubGhzProtocolDecoderSiemensVdo {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;
    uint16_t sync_shift; // sliding 16-bit window while hunting for the sync word
    uint16_t sync_bits; // number of bits shifted in (saturates at 16)
    bool synced; // sync word found, now collecting payload
    bool invert; // sync matched inverted -> invert captured bits
    uint16_t data_bit_count; // payload bits collected so far
    uint8_t bytes[SIEMENS_VDO_PAYLOAD_BYTES];
    uint8_t crc; // captured check byte
    bool crc_check; // XOR integrity result
};

struct SubGhzProtocolEncoderSiemensVdo {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

const SubGhzProtocolDecoder subghz_protocol_siemens_vdo_decoder = {
    .alloc = subghz_protocol_decoder_siemens_vdo_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_siemens_vdo_feed,
    .reset = subghz_protocol_decoder_siemens_vdo_reset,

    .get_hash_data = subghz_protocol_decoder_siemens_vdo_get_hash_data,
    .serialize = subghz_protocol_decoder_siemens_vdo_serialize,
    .deserialize = subghz_protocol_decoder_siemens_vdo_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

// Rolling / encrypted code: no transmit support (stub encoder).
const SubGhzProtocolEncoder subghz_protocol_siemens_vdo_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

const SubGhzProtocol subghz_protocol_siemens_vdo = {
    .name = SIEMENS_VDO_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,

    .decoder = &subghz_protocol_siemens_vdo_decoder,
    .encoder = &subghz_protocol_siemens_vdo_encoder,
};

// ============================================================================
// Field recovery (works for both live decode and file reload)
//
// generic.data holds bytes[4..11] packed big-endian; generic.serial holds the
// ID (bytes[3..0]). Derive button / sequence / encrypted / crc from data.
// ============================================================================

static void siemens_vdo_check_remote_controller(SubGhzProtocolDecoderSiemensVdo* instance) {
    uint64_t data = instance->generic.data;
    instance->generic.btn = (uint8_t)((data >> 56) & 0xFF);
    instance->generic.cnt = (uint32_t)((data >> 40) & 0xFFFF);
    instance->crc = (uint8_t)(data & 0xFF);
}

// ============================================================================
// Frame validation + field extraction
// ============================================================================

static void siemens_vdo_process_frame(SubGhzProtocolDecoderSiemensVdo* instance) {
    uint8_t b[SIEMENS_VDO_PAYLOAD_BYTES];
    for(uint8_t i = 0; i < SIEMENS_VDO_PAYLOAD_BYTES; i++) {
        b[i] = instance->invert ? (uint8_t)(~instance->bytes[i]) : instance->bytes[i];
    }

    // Sanity gate (rtl_433 DECODE_FAIL_SANITY): reject flat all-0x00 / all-0xFF.
    uint16_t sum = 0;
    for(uint8_t i = 0; i < SIEMENS_VDO_PAYLOAD_BYTES; i++) {
        sum += b[i];
    }
    if(sum == 0 || sum == (uint16_t)(0xFF * SIEMENS_VDO_PAYLOAD_BYTES)) {
        return;
    }

    // MIC gate (rtl_433 DECODE_FAIL_MIC): XOR of all 12 bytes must be zero.
    uint8_t x = 0;
    for(uint8_t i = 0; i < SIEMENS_VDO_PAYLOAD_BYTES; i++) {
        x ^= b[i];
    }
    if(x != 0) {
        return;
    }

    // Parse payload.
    uint32_t id = ((uint32_t)b[3] << 24) | ((uint32_t)b[2] << 16) | ((uint32_t)b[1] << 8) |
                  (uint32_t)b[0];

    // Pack bytes[4..11] big-endian into the 64-bit generic.data (Key field).
    uint64_t data = 0;
    for(uint8_t i = 4; i < SIEMENS_VDO_PAYLOAD_BYTES; i++) {
        data = (data << 8) | (uint64_t)b[i];
    }

    instance->generic.serial = id;
    instance->generic.data = data;
    instance->generic.data_count_bit = SIEMENS_VDO_PAYLOAD_BITS;
    siemens_vdo_check_remote_controller(instance);
    instance->crc_check = true;

    // Feed the hash helper (ppa_decoder_hash_blocks reads decoder.decode_*).
    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = SIEMENS_VDO_PAYLOAD_BITS;

    FURI_LOG_D(
        TAG,
        "Frame ok: Id:%08lX Btn:%02X Seq:%04lX",
        (unsigned long)id,
        instance->generic.btn,
        (unsigned long)instance->generic.cnt);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

// ============================================================================
// Decoder: alloc / free / reset
// ============================================================================

void* subghz_protocol_decoder_siemens_vdo_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderSiemensVdo* instance =
        calloc(1, sizeof(SubGhzProtocolDecoderSiemensVdo));
    instance->base.protocol = &subghz_protocol_siemens_vdo;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}



void subghz_protocol_decoder_siemens_vdo_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderSiemensVdo* instance = context;
    instance->decoder.parser_step = 0;
    instance->sync_shift = 0;
    instance->sync_bits = 0;
    instance->synced = false;
    instance->invert = false;
    instance->data_bit_count = 0;
    manchester_advance(
        instance->manchester_state, ManchesterEventReset, &instance->manchester_state, NULL);
}

// ============================================================================
// Decoder: feed
//
// Every RF edge (level + duration) is classified into a Manchester event and
// fed to the toolbox Manchester state machine. Recovered bits are shifted into
// a 16-bit window while hunting for the 0x6001 sync word (or its inverse);
// after sync we collect 96 payload bits MSB-first into bytes[] and validate.
// ============================================================================

void subghz_protocol_decoder_siemens_vdo_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderSiemensVdo* instance = context;

    ManchesterEvent event =
        ppa_manchester_event(duration, level, &subghz_protocol_siemens_vdo_const);

    if(event == ManchesterEventReset) {
        // Gap / out-of-range pulse: abandon any in-progress frame and resync.
        instance->sync_shift = 0;
        instance->sync_bits = 0;
        instance->synced = false;
        instance->invert = false;
        instance->data_bit_count = 0;
        manchester_advance(
            instance->manchester_state, ManchesterEventReset, &instance->manchester_state, NULL);
        return;
    }

    bool data = false;
    if(!manchester_advance(
           instance->manchester_state, event, &instance->manchester_state, &data)) {
        return; // half-bit consumed, no full data bit yet
    }

    if(!instance->synced) {
        instance->sync_shift = (uint16_t)((instance->sync_shift << 1) | (data ? 1U : 0U));
        if(instance->sync_bits < 16) instance->sync_bits++;

        if(instance->sync_bits >= 16) {
            if(instance->sync_shift == SIEMENS_VDO_SYNC) {
                instance->synced = true;
                instance->invert = false;
            } else if(instance->sync_shift == SIEMENS_VDO_SYNC_INV) {
                instance->synced = true;
                instance->invert = true;
            }
            if(instance->synced) {
                instance->data_bit_count = 0;
                for(uint8_t i = 0; i < SIEMENS_VDO_PAYLOAD_BYTES; i++) instance->bytes[i] = 0;
            }
        }
        return;
    }

    // Collecting payload: store bit MSB-first into the 12-byte buffer.
    uint16_t idx = instance->data_bit_count >> 3;
    if(idx < SIEMENS_VDO_PAYLOAD_BYTES && data) {
        instance->bytes[idx] |= (uint8_t)(1U << (7 - (instance->data_bit_count & 7)));
    }
    instance->data_bit_count++;

    if(instance->data_bit_count >= SIEMENS_VDO_PAYLOAD_BITS) {
        siemens_vdo_process_frame(instance);
        // Reset for the next repeat within the same RX session.
        instance->synced = false;
        instance->invert = false;
        instance->sync_shift = 0;
        instance->sync_bits = 0;
        instance->data_bit_count = 0;
    }
}

// ============================================================================
// Decoder: hash / serialize / deserialize / get_string
// ============================================================================

uint8_t subghz_protocol_decoder_siemens_vdo_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderSiemensVdo* instance = context;
    // Payload is 96 bits; decode_data is only 64 bits wide, so hash exactly the
    // 8 bytes it holds (bytes[4..11]) rather than (count/8)+1 which would
    // over-read past decode_data.
    return subghz_protocol_blocks_get_hash_data(&instance->decoder, sizeof(uint64_t));
}

SubGhzProtocolStatus subghz_protocol_decoder_siemens_vdo_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderSiemensVdo* instance = context;

    siemens_vdo_check_remote_controller(instance);

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    return ppa_serialize_fields(
        flipper_format,
        PP_FIELD_SERIAL | PP_FIELD_BTN | PP_FIELD_CNT,
        instance->generic.serial,
        instance->generic.btn,
        instance->generic.cnt,
        0);
}

SubGhzProtocolStatus
    subghz_protocol_decoder_siemens_vdo_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderSiemensVdo* instance = context;
    flipper_format_rewind(flipper_format);
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_siemens_vdo_const.min_count_bit_for_found);
}

