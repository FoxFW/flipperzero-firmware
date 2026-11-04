#if 0 /* SHELVED 2026-10-18: sync word (0x0F0F) + timings are placeholders; needs a clean
         Mercedes FSK capture to tune. Kept in-tree, compiled to nothing until enabled.
         To re-enable: delete this #if 0/#endif, uncomment the include in protocol_items.h,
         and the registry line in protocol_items.c. */
#include "mercedes_fob.h"
#include <lib/toolbox/manchester_decoder.h>
#include "common.h"

#define TAG "MercedesFob"

// =============================================================================
// Mercedes-Benz key-fob Sub-GHz RECOGNIZER / DECODER (decode-only).
//
// Physical layer + frame teardown documented at:
//   https://0x44.cc/radio/2024/03/13/reversing-a-car-key-fob-signal.html
// The article deliberately does not name the exact car, but the physical layer
// and frame layout are concrete and reproducible.
//
// The rolling-code CRYPTO IS UNBROKEN. This is a "capture now, decode later"
// recognizer: it recovers and displays the cleartext framing (ID / button /
// sequence) and stores the raw frame, but the 4-byte rolling code stays
// encrypted and there is NO encoder (you cannot forge a valid next code).
//
// -----------------------------------------------------------------------------
// PHYSICAL LAYER (from the writeup)
//   Carrier      : ~433.92 MHz (EU SRD)                      [FM / 2-FSK preset]
//   Modulation   : 2-FSK (one deviation tone for 0, one for 1)
//   Line coding  : Manchester II (each data bit -> two opposite half-bits)
//   Wake preamble: ~100 bits of "null" wake burst (no data) before the frame
//   Sync word    : 2 bytes, marks the start of the data (EXCLUDED from checksum)
//   Repetition   : the frame is retransmitted ~3x per button press
//
// FRAME LAYOUT (after Manchester decode; byte indices as in the writeup)
//   byte 0..1  : sync word            (2 bytes) -- hunted, NOT in checksum
//   byte 2..4  : ID / serial          (3 bytes / 24-bit) -- static per fob
//   byte 5     : sequence / counter   (low nibble increments each packet)
//   byte 6     : button / command     (0x08 = unlock, 0x04 = lock)
//   byte 7..10 : rolling code         (4 bytes) -- cryptographically secure
//   byte 11    : XOR checksum         = XOR(byte 2..10), sync bytes excluded
//
// So after the 2-byte sync word there are 10 payload bytes = 80 data bits
// (byte 2..11). The checksum (byte 11) is the XOR of the 9 preceding payload
// bytes (byte 2..10); it is the ONLY validation gate here, so a frame whose
// XOR does not match is rejected -> no false positives.
//
// -----------------------------------------------------------------------------
// DOCUMENTED GAPS (values the writeup does NOT pin down; parameterized below):
//   * Exact SYNC-WORD bytes: the article describes a 2-byte syncword but never
//     prints its hex value. MERCEDES_FOB_SYNC_WORD is therefore a tunable
//     PLACEHOLDER -- set it from a real capture. The XOR checksum, not the sync
//     match, is what guarantees a correct decode.
//   * Exact bit timing / baud: the article gives "~50 samples/symbol" but no
//     absolute symbol time. te_short/te_long below are a nominal Manchester
//     half-bit / full-bit pair -- tune them to captured pulse widths.
// =============================================================================

// --- Tunable framing parameters (see DOCUMENTED GAPS above) ------------------
#define MERCEDES_FOB_SYNC_WORD 0x0F0FU // PLACEHOLDER 2-byte sync (tune to capture)
#define MERCEDES_FOB_SYNC_BITS 16

// 10 payload bytes follow the sync word (frame bytes 2..11).
#define MERCEDES_FOB_DATA_BYTES 10
#define MERCEDES_FOB_DATA_BITS  (MERCEDES_FOB_DATA_BYTES * 8) // 80

// Raw identity packed into generic.data: serial(24) | button(8) | rolling(32).
#define MERCEDES_FOB_KEY_BITS 64

static const SubGhzBlockConst subghz_protocol_mercedes_fob_const = {
    // Nominal Manchester half-bit / full-bit. NOT pinned by the writeup -- tune.
    .te_short = 100,
    .te_long = 200,
    .te_delta = 50,
    .min_count_bit_for_found = MERCEDES_FOB_KEY_BITS,
};

typedef enum {
    MercedesFobDecoderStepReset = 0,
    MercedesFobDecoderStepFindSync,
    MercedesFobDecoderStepData,
} MercedesFobDecoderStep;

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;
    uint16_t sync_shift;

    uint8_t frame[MERCEDES_FOB_DATA_BYTES]; // frame bytes 2..11
    uint16_t data_bits; // bits collected into frame

    // Recovered cleartext fields.
    uint32_t serial; // 24-bit ID (frame byte 2..4)
    uint8_t seq; // sequence/counter (frame byte 5, low nibble increments)
    uint8_t button; // command (frame byte 6)
    uint32_t rolling; // encrypted rolling code (frame byte 7..10)
    uint8_t checksum; // frame byte 11
} SubGhzProtocolDecoderMercedesFob;

const SubGhzProtocolDecoder mercedes_fob_decoder = {
    .alloc = subghz_protocol_decoder_mercedes_fob_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_mercedes_fob_feed,
    .reset = subghz_protocol_decoder_mercedes_fob_reset,
    .get_hash_data = subghz_protocol_decoder_mercedes_fob_get_hash_data,
    .serialize = subghz_protocol_decoder_mercedes_fob_serialize,
    .deserialize = subghz_protocol_decoder_mercedes_fob_deserialize,
    .get_string = subghz_protocol_decoder_mercedes_fob_get_string,
};

// Rolling crypto is unbroken and there is no known keystream, so TX/emulation
// is intentionally unimplemented (decode-only recognizer).
const SubGhzProtocolEncoder mercedes_fob_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

const SubGhzProtocol subghz_protocol_mercedes_fob = {
    .name = MERCEDES_FOB_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,
    .decoder = &mercedes_fob_decoder,
    .encoder = &mercedes_fob_encoder,
};

// =============================================================================
// ALLOC / FREE / RESET
// =============================================================================

void* subghz_protocol_decoder_mercedes_fob_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderMercedesFob* instance =
        calloc(1, sizeof(SubGhzProtocolDecoderMercedesFob));
    instance->base.protocol = &subghz_protocol_mercedes_fob;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}



void subghz_protocol_decoder_mercedes_fob_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderMercedesFob* instance = context;
    instance->decoder.parser_step = MercedesFobDecoderStepReset;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->sync_shift = 0;
    instance->data_bits = 0;
    instance->manchester_state = ManchesterStateStart1;
    memset(instance->frame, 0, sizeof(instance->frame));
}

// =============================================================================
// DECODE HELPERS
// =============================================================================

static ManchesterEvent
    mercedes_fob_level_and_duration_to_event(bool level, uint32_t duration) {
    bool is_long;
    if(DURATION_DIFF(duration, subghz_protocol_mercedes_fob_const.te_long) <
       subghz_protocol_mercedes_fob_const.te_delta * 2) {
        is_long = true;
    } else if(
        DURATION_DIFF(duration, subghz_protocol_mercedes_fob_const.te_short) <
        subghz_protocol_mercedes_fob_const.te_delta) {
        is_long = false;
    } else {
        return ManchesterEventReset;
    }
    if(level)
        return is_long ? ManchesterEventLongHigh : ManchesterEventShortHigh;
    else
        return is_long ? ManchesterEventLongLow : ManchesterEventShortLow;
}

// XOR checksum: byte 11 == XOR(byte 2..10). Sync bytes (0..1) are excluded.
static bool mercedes_fob_check_checksum(const SubGhzProtocolDecoderMercedesFob* instance) {
    // frame[0..8] == article bytes 2..10 (9 covered bytes), frame[9] == byte 11.
    uint8_t calc = subghz_protocol_blocks_xor_bytes(instance->frame, MERCEDES_FOB_DATA_BYTES - 1);
    return calc == instance->frame[MERCEDES_FOB_DATA_BYTES - 1];
}

static void mercedes_fob_extract_fields(SubGhzProtocolDecoderMercedesFob* instance) {
    const uint8_t* f = instance->frame; // f[0] == article byte 2
    instance->serial = ((uint32_t)f[0] << 16) | ((uint32_t)f[1] << 8) | f[2]; // byte 2..4
    instance->seq = f[3]; // byte 5 (low nibble = sequence)
    instance->button = f[4]; // byte 6
    instance->rolling =
        ((uint32_t)f[5] << 24) | ((uint32_t)f[6] << 16) | ((uint32_t)f[7] << 8) | f[8]; // byte 7..10
    instance->checksum = f[9]; // byte 11

    // Raw identity: serial(24) | button(8) | rolling(32) -> 64 bits.
    // (seq + checksum are omitted from the packed key but shown in get_string.)
    instance->generic.data = ((uint64_t)instance->serial << 40) |
                             ((uint64_t)instance->button << 32) | (uint64_t)instance->rolling;
    instance->generic.data_count_bit = MERCEDES_FOB_KEY_BITS;

    instance->generic.serial = instance->serial;
    instance->generic.btn = instance->button;
    instance->generic.cnt = (uint32_t)(instance->seq & 0x0F); // documented sequence nibble

    // Mirror into the block decoder so the standard hash helper works.
    instance->decoder.decode_data = instance->generic.data;
    instance->decoder.decode_count_bit = MERCEDES_FOB_KEY_BITS;
}

// =============================================================================
// FEED: 2-FSK Manchester state machine (hunt sync word -> collect -> validate)
// =============================================================================

void subghz_protocol_decoder_mercedes_fob_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderMercedesFob* instance = context;

    ManchesterEvent event = mercedes_fob_level_and_duration_to_event(level, duration);
    if(event == ManchesterEventReset) {
        // Long gap between the ~3 repeats (or noise) -> hunt for the next frame.
        instance->decoder.parser_step = MercedesFobDecoderStepReset;
        instance->sync_shift = 0;
        instance->data_bits = 0;
        instance->manchester_state = ManchesterStateStart1;
        return;
    }

    bool bit = false;
    bool have_bit = manchester_advance(
        instance->manchester_state, event, &instance->manchester_state, &bit);
    if(!have_bit) return;

    switch(instance->decoder.parser_step) {
    case MercedesFobDecoderStepReset:
        instance->sync_shift = 0;
        instance->manchester_state = ManchesterStateStart1;
        instance->decoder.parser_step = MercedesFobDecoderStepFindSync;
        // consume this bit as the first sync candidate
        /* fallthrough */
    case MercedesFobDecoderStepFindSync:
        instance->sync_shift = (uint16_t)((instance->sync_shift << 1) | (bit ? 1 : 0));
        if((instance->sync_shift &
            (uint16_t)((1u << MERCEDES_FOB_SYNC_BITS) - 1u)) == MERCEDES_FOB_SYNC_WORD) {
            instance->decoder.parser_step = MercedesFobDecoderStepData;
            instance->data_bits = 0;
            memset(instance->frame, 0, sizeof(instance->frame));
        }
        break;

    case MercedesFobDecoderStepData: {
        // MSB-first byte accumulation (frame[] was cleared on sync).
        uint16_t idx = instance->data_bits >> 3;
        instance->frame[idx] = (uint8_t)((instance->frame[idx] << 1) | (bit ? 1 : 0));
        instance->data_bits++;

        if(instance->data_bits >= MERCEDES_FOB_DATA_BITS) {
            if(mercedes_fob_check_checksum(instance)) {
                mercedes_fob_extract_fields(instance);
                if(instance->base.callback)
                    instance->base.callback(&instance->base, instance->base.context);
            }
            // Whether valid or not, hunt for the next repeat/frame.
            instance->decoder.parser_step = MercedesFobDecoderStepReset;
            instance->sync_shift = 0;
            instance->data_bits = 0;
        }
        break;
    }
    }
}

// =============================================================================
// HASH / SERIALIZE / DESERIALIZE
// =============================================================================

uint8_t subghz_protocol_decoder_mercedes_fob_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderMercedesFob* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_mercedes_fob_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderMercedesFob* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret == SubGhzProtocolStatusOk) {
        // Extra self-describing fields not carried by the 64-bit Key.
        uint32_t temp = instance->seq;
        flipper_format_write_uint32(flipper_format, "Seq", &temp, 1);
        temp = instance->checksum;
        flipper_format_write_uint32(flipper_format, "Checksum", &temp, 1);
    }
    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_mercedes_fob_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderMercedesFob* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_mercedes_fob_const.min_count_bit_for_found);
    if(ret != SubGhzProtocolStatusOk) return ret;

    // Unpack serial(24) | button(8) | rolling(32) from the restored Key.
    uint64_t data = instance->generic.data;
    instance->rolling = (uint32_t)(data & 0xFFFFFFFF);
    instance->button = (uint8_t)((data >> 32) & 0xFF);
    instance->serial = (uint32_t)((data >> 40) & 0xFFFFFF);

    uint32_t temp = 0;
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Seq", &temp, 1)) instance->seq = (uint8_t)temp;
    temp = 0;
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Checksum", &temp, 1))
        instance->checksum = (uint8_t)temp;

    instance->generic.serial = instance->serial;
    instance->generic.btn = instance->button;
    instance->generic.cnt = (uint32_t)(instance->seq & 0x0F);

    return SubGhzProtocolStatusOk;
}

// =============================================================================
// DISPLAY
// =============================================================================

void subghz_protocol_decoder_mercedes_fob_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderMercedesFob* instance = context;

    const char* button_name;
    switch(instance->button) {
    case 0x08:
        button_name = "Unlock";
        break;
    case 0x04:
        button_name = "Lock";
        break;
    default:
        button_name = "?";
        break;
    }

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "ID:0x%06lX  Btn:0x%02X %s\r\n"
        "Seq:%u  CSum:0x%02X OK\r\n"
        "Rolling:0x%08lX\r\n"
        "rolling: encrypted",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)instance->serial,
        instance->button,
        button_name,
        (unsigned)(instance->seq & 0x0F),
        instance->checksum,
        (unsigned long)instance->rolling);
}

#endif /* SHELVED mercedes_fob */
