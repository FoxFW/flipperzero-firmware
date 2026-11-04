#include "mic6sc2.h"
#include "protocols_common.h"

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

/*
 * Microchip 6SC2 — Car Remote (315.1 MHz).
 *
 * Ported from the rtl_433 device decoder `mic6sc2_car_remote.c`
 * (Copyright (C) 2024 Ethan Halsall, GPLv2), MSOP-10 keyfob IC,
 * models 6SC2 CMGU / 6SC2 CDFA.
 *
 * Modulation : OOK_PULSE_MANCHESTER_ZEROBIT
 * short_width: 250 us  (Manchester half-bit / clock)  => full bit ~500 us
 * reset_limit: 10000 us (inter-frame gap)
 *
 * Over-the-air the bytes are transmitted LSB-first ("reflected"); rtl_433
 * packs the received bit order MSB-first so the raw preamble bytes read
 * 0x55 0x54, and reverse8()'s the payload bytes to recover their true value.
 *
 * Frame layout (rtl_433 byte view, up to 88 bits / 11 bytes):
 *
 *     PPPP EEEEEEEE bbbb uuuu SSSS CC
 *
 *   b[0..1]  : 16-bit preamble  (raw 0x55 0x54)
 *   b[2..5]  : 32-bit encrypted (rolling code), reverse8 per byte, big-endian
 *   b[6]     : reverse8 -> low nibble = button, high nibble = unknown
 *   b[7..8]  : 16-bit sequence  (reverse8(b[8])<<8 | reverse8(b[7]))
 *   b[9]     : 8-bit checksum
 *   b[10]    : trailing byte (0 for an 80-bit frame)
 *
 * Integrity: xor_bytes(b + 2, 9) == 0, i.e. the XOR of b[2]..b[10] is zero.
 * A frame that fails either the fixed preamble or this checksum is rejected,
 * which keeps this OOK Manchester decoder from emitting on noise.
 */

#define TAG "MIC6SC2"

// Manchester half-bit is the clock; a full bit is two half-bits.
static const SubGhzBlockConst subghz_protocol_mic6sc2_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 100,
    .min_count_bit_for_found = 64,
};

#define MIC6SC2_MAX_BYTES 11U /* 88 bits */
#define MIC6SC2_MAX_BITS  88U
#define MIC6SC2_MIN_BITS  80U /* 10 bytes: b[0..9], b[10] treated as 0 */

typedef struct SubGhzProtocolDecoderMic6sc2 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;

    uint8_t frame[MIC6SC2_MAX_BYTES];
    uint16_t bit_count;

    uint32_t encrypted;
    uint16_t sequence;
    uint8_t button;
} SubGhzProtocolDecoderMic6sc2;

// =============================================================================
// PROTOCOL INTERFACE DEFINITIONS
// =============================================================================

const SubGhzProtocolDecoder subghz_protocol_mic6sc2_decoder = {
    .alloc = subghz_protocol_decoder_mic6sc2_alloc,
    .free = pp_decoder_free_default,
    .feed = subghz_protocol_decoder_mic6sc2_feed,
    .reset = subghz_protocol_decoder_mic6sc2_reset,
    .get_hash_data = subghz_protocol_decoder_mic6sc2_get_hash_data,
    .serialize = subghz_protocol_decoder_mic6sc2_serialize,
    .deserialize = subghz_protocol_decoder_mic6sc2_deserialize,
    .get_string = subghz_protocol_decoder_mic6sc2_get_string,
};

// Rolling-code fob: no TX. Shared NULL encoder stub.
const SubGhzProtocolEncoder subghz_protocol_mic6sc2_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

const SubGhzProtocol subghz_protocol_mic6sc2 = {
    .name = MIC6SC2_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_mic6sc2_decoder,
    .encoder = &subghz_protocol_mic6sc2_encoder,
};

// =============================================================================
// DECODER IMPLEMENTATION
// =============================================================================

static void mic6sc2_soft_reset(SubGhzProtocolDecoderMic6sc2* instance) {
    memset(instance->frame, 0, sizeof(instance->frame));
    instance->bit_count = 0;
    manchester_advance(
        instance->manchester_state, ManchesterEventReset, &instance->manchester_state, NULL);
}

// Validate the assembled frame and, on success, populate generic + emit.
static bool mic6sc2_process_frame(SubGhzProtocolDecoderMic6sc2* instance) {
    if(instance->bit_count < MIC6SC2_MIN_BITS) return false;

    const uint8_t* b = instance->frame;

    // Fixed 16-bit preamble (raw, un-reflected).
    if(b[0] != 0x55 || b[1] != 0x54) return false;

    // Integrity: XOR of b[2]..b[10] must be zero (rtl_433 xor_bytes(b+2, 9)).
    // For an 80-bit frame b[10] is 0 (buffer was cleared), matching rtl_433's
    // zero-padded bitbuffer row.
    uint8_t xor_sum = 0;
    for(uint8_t i = 2; i < 11; i++) {
        xor_sum ^= b[i];
    }
    if(xor_sum != 0) return false;

    // The transmission is LSB first, big endian: reverse8 each payload byte.
    instance->encrypted = ((uint32_t)pp_reverse_bits8(b[5]) << 24) |
                          ((uint32_t)pp_reverse_bits8(b[4]) << 16) |
                          ((uint32_t)pp_reverse_bits8(b[3]) << 8) |
                          (uint32_t)pp_reverse_bits8(b[2]);
    instance->button = pp_reverse_bits8(b[6]) & 0x0F;
    instance->sequence =
        (uint16_t)((pp_reverse_bits8(b[8]) << 8) | pp_reverse_bits8(b[7]));

    // Store the 8 core payload bytes (b[2..9]) big-endian as the 64-bit Key.
    uint64_t data = 0;
    for(uint8_t i = 2; i < 10; i++) {
        data = (data << 8) | b[i];
    }

    instance->generic.data = data;
    instance->generic.data_count_bit = 64;
    instance->generic.serial = instance->encrypted;
    instance->generic.btn = instance->button;
    instance->generic.cnt = instance->sequence;

    // Feed the block hash helper a stable per-frame value.
    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = 64;

    FURI_LOG_D(
        TAG,
        "Frame: %ubit Enc:%08lX Btn:%01X Seq:%04X",
        instance->bit_count,
        (unsigned long)instance->encrypted,
        instance->button,
        instance->sequence);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
    return true;
}

void* subghz_protocol_decoder_mic6sc2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderMic6sc2* instance = calloc(1, sizeof(SubGhzProtocolDecoderMic6sc2));
    instance->base.protocol = &subghz_protocol_mic6sc2;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_mic6sc2_free(void* context) {
    furi_check(context);
    free(context);
}

void subghz_protocol_decoder_mic6sc2_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderMic6sc2* instance = context;
    instance->manchester_state = ManchesterStateMid1;
    memset(instance->frame, 0, sizeof(instance->frame));
    instance->bit_count = 0;
    instance->encrypted = 0;
    instance->sequence = 0;
    instance->button = 0;
}

// Manchester slicer over the raw pulse stream. There is no pre-demodulated
// bitstream here (unlike rtl_433): each LevelDuration edge drives the toolbox
// Manchester state machine, and full frames are gated on the preamble + XOR.
void subghz_protocol_decoder_mic6sc2_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderMic6sc2* instance = context;

    ManchesterEvent event =
        pp_manchester_event(duration, level, &subghz_protocol_mic6sc2_const);

    if(event == ManchesterEventReset) {
        // Out-of-range duration = inter-frame gap / end of transmission.
        // A frame that reached full length terminates on this gap.
        mic6sc2_process_frame(instance);
        mic6sc2_soft_reset(instance);
        return;
    }

    bool data_bit;
    if(!manchester_advance(
           instance->manchester_state, event, &instance->manchester_state, &data_bit)) {
        return;
    }

    // Append bit MSB-first into the frame byte buffer.
    if(instance->bit_count < MIC6SC2_MAX_BITS) {
        if(data_bit) {
            instance->frame[instance->bit_count >> 3] |=
                (uint8_t)(1U << (7U - (instance->bit_count & 7U)));
        }
        instance->bit_count++;
    }

    // Sync gate: once the preamble bytes are complete, verify them. A mismatch
    // means we locked onto the wrong phase/position — drop and wait for the
    // next gap to realign (the fob repeats the frame).
    if(instance->bit_count == 16) {
        if(instance->frame[0] != 0x55 || instance->frame[1] != 0x54) {
            mic6sc2_soft_reset(instance);
            return;
        }
    }

    // Full-length frame: validate and emit, then realign for the repeat.
    if(instance->bit_count >= MIC6SC2_MAX_BITS) {
        mic6sc2_process_frame(instance);
        mic6sc2_soft_reset(instance);
    }
}

uint8_t subghz_protocol_decoder_mic6sc2_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderMic6sc2* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8U) + 1U);
}

SubGhzProtocolStatus subghz_protocol_decoder_mic6sc2_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderMic6sc2* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    if(ret == SubGhzProtocolStatusOk) {
        pp_serialize_fields(
            flipper_format,
            PP_FIELD_SERIAL | PP_FIELD_BTN | PP_FIELD_CNT,
            instance->generic.serial,
            instance->generic.btn,
            instance->generic.cnt,
            0);
    }

    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_mic6sc2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderMic6sc2* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_mic6sc2_const.min_count_bit_for_found);

    if(ret == SubGhzProtocolStatusOk) {
        // Recover the display fields from the stored 64-bit payload (b[2..9]).
        uint64_t data = instance->generic.data;
        uint8_t b[8];
        for(int i = 7; i >= 0; i--) {
            b[i] = (uint8_t)(data & 0xFF);
            data >>= 8;
        }
        // b[0..3] = encrypted, b[4] = button byte, b[5..6] = sequence.
        instance->encrypted = ((uint32_t)pp_reverse_bits8(b[3]) << 24) |
                              ((uint32_t)pp_reverse_bits8(b[2]) << 16) |
                              ((uint32_t)pp_reverse_bits8(b[1]) << 8) |
                              (uint32_t)pp_reverse_bits8(b[0]);
        instance->button = pp_reverse_bits8(b[4]) & 0x0F;
        instance->sequence =
            (uint16_t)((pp_reverse_bits8(b[6]) << 8) | pp_reverse_bits8(b[5]));

        uint32_t serial = instance->generic.serial;
        uint32_t btn = instance->generic.btn;
        uint32_t cnt = instance->generic.cnt;
        pp_encoder_read_fields(flipper_format, &serial, &btn, &cnt, NULL);
        instance->generic.serial = serial;
        instance->generic.btn = (uint8_t)btn;
        instance->generic.cnt = cnt;
        instance->encrypted = serial;
        instance->button = (uint8_t)btn;
        instance->sequence = (uint16_t)cnt;
    }

    return ret;
}

void subghz_protocol_decoder_mic6sc2_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderMic6sc2* instance = context;

    const char* button_name;
    switch(instance->button) {
    case 0x1:
        button_name = "Unlock";
        break;
    case 0x2:
        button_name = "Lock";
        break;
    case 0x3:
        button_name = "Trunk";
        break;
    case 0x4:
        button_name = "Panic";
        break;
    default:
        button_name = "?";
        break;
    }

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key: %016llX\r\n"
        "Enc: %08lX\r\n"
        "Btn: %01X - %s\r\n"
        "Seq: %04X  CS:OK\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long long)instance->generic.data,
        (unsigned long)instance->encrypted,
        instance->button,
        button_name,
        instance->sequence);
}
