#include "lexus_v0.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include <lib/subghz/blocks/bitbuf.h>

#define TAG "LexusV0"

// Lexus V0 -- ported from firmware lexus_v0.c via KeeLoq-RX arke_toyota.py.
// Manchester rolling code. Custom timing bands (NOT DURATION_DIFF): preamble
// pulse in [250,470]; LONG in [500,980]; SHORT < 500; inter-frame GAP >= 980.
// Preamble >= 16 clean pulses, the first LONG triggers data (the LONG is the
// first data bit). Manchester: one LONG = one bit (= level); two SHORTs = one
// bit (= the 2nd short's level) via a half-bit toggle.
//
// Frame = 224 bits: [0..31] header (discarded) | [32..191] 160-bit encrypted
// rolling code (20 bytes) | [192..223] 32-bit serial. There is NO per-frame CRC.
// The only validator is the Toyota complement signature: the serial alternates
// between S and ~S on successive frames, so a frame is marked complement-verified
// only once its ~S partner has also been seen. A lone serial stays unverified
// (honest). Degenerate serials (all-0 / all-1) are rejected as noise.
//
// Storage: generic.data holds the TOP 64 bits of the rolling code (first 8 of the
// 20 payload bytes); the full 20-byte payload + 32-bit serial live in the struct.
// Decode-only (encrypted rolling code); .encoder = NULL.

#define LEXUS_V0_PRE_LO 250
#define LEXUS_V0_PRE_HI 470
#define LEXUS_V0_LONG_LO 500
#define LEXUS_V0_LONG_HI 980
#define LEXUS_V0_GAP 980
#define LEXUS_V0_MIN_PRE 16
#define LEXUS_V0_HEADER_BITS 32
#define LEXUS_V0_DATA_BITS 160
#define LEXUS_V0_SERIAL_BITS 32
#define LEXUS_V0_FRAME_BITS 224
#define LEXUS_V0_DATA_BYTES 20

static const SubGhzBlockConst subghz_protocol_lexus_v0_const = {
    .te_short = 350,
    .te_long = 700,
    .te_delta = 150,
    .min_count_bit_for_found = LEXUS_V0_FRAME_BITS,
};

struct SubGhzProtocolDecoderLexusV0 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t step; // 0 reset, 1 preamble, 2 data
    uint16_t preamble_count;
    bool half_bit;
    uint16_t bit_count;
    uint8_t all_bits[SUBGHZ_BITBUF_BYTES(LEXUS_V0_FRAME_BITS + 8)];

    uint8_t rolling[LEXUS_V0_DATA_BYTES]; // full 160-bit payload
    uint32_t serial;

    // Toyota complement-signature cross-frame validator.
    uint32_t prev_serial;
    bool have_prev;
    bool compl_ok;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderLexusV0);


typedef enum {
    LexusV0StepReset = 0,
    LexusV0StepPreamble = 1,
    LexusV0StepData = 2,
} LexusV0Step;

static void lexus_v0_restart(SubGhzProtocolDecoderLexusV0* instance) {
    instance->step = LexusV0StepReset;
    instance->preamble_count = 0;
    instance->half_bit = false;
    instance->bit_count = 0;
}

// Mirror of arke LexusV0._process_frame + emit (complement-signature validation).
static void lexus_v0_process_frame(SubGhzProtocolDecoderLexusV0* instance) {
    if(instance->bit_count < LEXUS_V0_FRAME_BITS) return;
    const uint8_t* b = instance->all_bits;

    uint32_t serial = 0;
    int base = LEXUS_V0_HEADER_BITS + LEXUS_V0_DATA_BITS; // 192
    for(int i = 0; i < LEXUS_V0_SERIAL_BITS; i++) {
        serial = (serial << 1) | (subghz_bitbuf_get(b, base + i) & 1U);
    }
    if(serial == 0x00000000U || serial == 0xFFFFFFFFU) return; // degenerate = noise

    uint8_t rolling[LEXUS_V0_DATA_BYTES];
    for(int k = 0; k < LEXUS_V0_DATA_BYTES; k++) {
        uint8_t v = 0;
        for(int j = 0; j < 8; j++) {
            v = (v << 1) | (subghz_bitbuf_get(b, LEXUS_V0_HEADER_BITS + k * 8 + j) & 1U);
        }
        rolling[k] = v;
    }

    uint64_t data64 = 0;
    for(int k = 0; k < 8; k++) data64 = (data64 << 8) | rolling[k];

    // complement signature: verified once a ~S partner has been seen.
    bool compl_ok = instance->have_prev && (serial == ((~instance->prev_serial) & 0xFFFFFFFFU));
    instance->prev_serial = serial;
    instance->have_prev = true;
    instance->compl_ok = compl_ok;

    memcpy(instance->rolling, rolling, LEXUS_V0_DATA_BYTES);
    instance->serial = serial;

    instance->generic.data = data64; // top 64 bits of the rolling code
    instance->generic.data_count_bit = LEXUS_V0_FRAME_BITS;
    instance->generic.serial = serial;
    instance->generic.btn = 0; // encrypted -- no cleartext button
    instance->generic.cnt = 0;
    instance->decoder.decode_data = data64;
    instance->decoder.decode_count_bit = LEXUS_V0_FRAME_BITS;

    if(instance->base.callback) instance->base.callback(&instance->base, instance->base.context);
}

void* subghz_protocol_decoder_lexus_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderLexusV0* instance = calloc(1, sizeof(SubGhzProtocolDecoderLexusV0));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_lexus_v0;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}



void subghz_protocol_decoder_lexus_v0_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderLexusV0* instance = context;
    lexus_v0_restart(instance);
    // NB: prev_serial/have_prev persist across frames within a capture (the
    // complement signature is cross-frame); they are cleared only on alloc.
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_lexus_v0_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderLexusV0* instance = context;

    bool in_pre = (duration >= LEXUS_V0_PRE_LO) && (duration <= LEXUS_V0_PRE_HI);
    bool is_long = (duration >= LEXUS_V0_LONG_LO) && (duration <= LEXUS_V0_LONG_HI);
    bool is_short = (duration < LEXUS_V0_LONG_LO);

    switch(instance->step) {
    case LexusV0StepReset:
        if(in_pre) {
            instance->preamble_count = 1;
            instance->step = LexusV0StepPreamble;
        }
        break;

    case LexusV0StepPreamble:
        if(in_pre) {
            instance->preamble_count++;
        } else if(is_long && instance->preamble_count >= LEXUS_V0_MIN_PRE) {
            instance->step = LexusV0StepData;
            instance->bit_count = 0;
            instance->half_bit = false;
            subghz_bitbuf_set(instance->all_bits, instance->bit_count, level ? 1 : 0);
            instance->bit_count++;
        } else {
            lexus_v0_restart(instance);
        }
        break;

    case LexusV0StepData:
        if(duration >= LEXUS_V0_GAP) {
            lexus_v0_process_frame(instance);
            lexus_v0_restart(instance);
            return;
        }
        if(is_long) {
            if(instance->half_bit) {
                lexus_v0_process_frame(instance);
                lexus_v0_restart(instance);
                return;
            } else {
                subghz_bitbuf_set(instance->all_bits, instance->bit_count, level ? 1 : 0);
                instance->bit_count++;
            }
        } else if(is_short) {
            if(instance->half_bit) {
                subghz_bitbuf_set(instance->all_bits, instance->bit_count, level ? 1 : 0);
                instance->bit_count++;
                instance->half_bit = false;
            } else {
                instance->half_bit = true;
            }
        }
        if(instance->bit_count >= LEXUS_V0_FRAME_BITS) {
            lexus_v0_process_frame(instance);
            lexus_v0_restart(instance);
        }
        break;

    default:
        lexus_v0_restart(instance);
        break;
    }
}



SubGhzProtocolStatus subghz_protocol_decoder_lexus_v0_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderLexusV0* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t serial = instance->serial;
    uint32_t compl_ok = instance->compl_ok ? 1 : 0;
    if(!flipper_format_write_uint32(flipper_format, "Serial", &serial, 1) ||
       !flipper_format_write_uint32(flipper_format, "ComplOk", &compl_ok, 1) ||
       !flipper_format_write_hex(flipper_format, "Rolling", instance->rolling, LEXUS_V0_DATA_BYTES)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_lexus_v0_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderLexusV0* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_lexus_v0_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;

    uint32_t serial = 0;
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Serial", &serial, 1)) {
        instance->serial = serial;
    } else {
        instance->serial = instance->generic.serial;
    }

    uint32_t compl_ok = 0;
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "ComplOk", &compl_ok, 1);
    instance->compl_ok = compl_ok ? true : false;

    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(
           flipper_format, "Rolling", instance->rolling, LEXUS_V0_DATA_BYTES)) {
        // fall back to the top-64 bits stored in generic.data
        for(int k = 0; k < 8; k++) {
            instance->rolling[k] = (uint8_t)(instance->generic.data >> (56 - 8 * k));
        }
    }

    instance->generic.serial = instance->serial;
    instance->generic.btn = 0;
    instance->generic.cnt = 0;
    instance->decoder.decode_data = instance->generic.data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

const SubGhzProtocolDecoder subghz_protocol_lexus_v0_decoder = {
    .alloc = subghz_protocol_decoder_lexus_v0_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_lexus_v0_feed,
    .reset = subghz_protocol_decoder_lexus_v0_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_lexus_v0_serialize,
    .deserialize = subghz_protocol_decoder_lexus_v0_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

const SubGhzProtocol subghz_protocol_lexus_v0 = {
    .name = LEXUS_V0_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_lexus_v0_decoder,
    .encoder = NULL,
};
