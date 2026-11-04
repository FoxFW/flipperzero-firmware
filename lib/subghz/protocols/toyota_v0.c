/**
 * Toyota V0 SubGHz Protocol — 96-bit encrypted rolling code with complement validation
 *
 * Physical: OOK Manchester @ 314.35 MHz
 * Manchester: te_short=750us, te_long=1500us
 *
 * Frame: [Preamble: alternating shorts] [Data: 96 bits] [Complement: 96 bits (~Data)]
 * Total: 192 Manchester bits per frame, repeated 4-5x per button press.
 *
 * Complement validation: each data byte XOR complement byte = 0xFF.
 * No sync word — complement check is the frame validator.
 *
 * Data appears fully encrypted (rolling code). No clearly separable
 * serial/button/counter fields. Within a burst, bytes 0 and 2 change
 * (likely rolling counter), bytes 3-11 are fixed per keyfob+button.
 *
 * Source signals: Toyota Estima 2000 keyfob @ 314.35 MHz
 */

#include "toyota_v0.h"

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

#define TAG "SubGhzProtocolToyotaV0"

#define TOYOTA_V0_DATA_BITS       88    // 11 bytes actual data
#define TOYOTA_V0_FRAME_BITS      184   // 88 data + 8 zero-gap + 88 complement
#define TOYOTA_V0_MIN_PREAMBLE    12    // minimum SHORT preamble pulses
#define TOYOTA_V0_TOTAL_BURSTS    5
#define TOYOTA_V0_PREAMBLE_PAIRS  20

static const SubGhzBlockConst subghz_protocol_toyota_v0_const = {
    .te_short = 750,
    .te_long = 1500,
    .te_delta = 250,
    .min_count_bit_for_found = TOYOTA_V0_DATA_BITS,
};

// ============================================================================
// DATA STRUCTURES
// ============================================================================

typedef enum {
    ToyotaV0DecoderStepReset = 0,
    ToyotaV0DecoderStepPreamble,
    ToyotaV0DecoderStepData,
} ToyotaV0DecoderStep;

struct SubGhzProtocolDecoderToyotaV0 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t preamble_count;
    bool half_bit;
    uint8_t all_bits[TOYOTA_V0_FRAME_BITS + 8];
    uint16_t bit_count;

    uint32_t data_32; // bits 64-95 (last 4 bytes)
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderToyotaV0);


struct SubGhzProtocolEncoderToyotaV0 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint32_t data_32;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderToyotaV0);


// ============================================================================
// PROTOCOL DEFINITION
// ============================================================================


const SubGhzProtocolDecoder subghz_protocol_toyota_v0_decoder = {
    .alloc = subghz_protocol_decoder_toyota_v0_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_toyota_v0_feed,
    .reset = subghz_protocol_decoder_toyota_v0_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_toyota_v0_serialize,
    .deserialize = subghz_protocol_decoder_toyota_v0_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_toyota_v0_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_toyota_v0");

const SubGhzProtocol subghz_protocol_toyota_v0 = {
    .name = SUBGHZ_PROTOCOL_TOYOTA_V0_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_toyota_v0_decoder,
    .encoder = &subghz_protocol_toyota_v0_encoder_ext.vtable,
};

// ============================================================================
// DECODER
// ============================================================================

void* subghz_protocol_decoder_toyota_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderToyotaV0* instance = malloc(sizeof(SubGhzProtocolDecoderToyotaV0));
    instance->base.protocol = &subghz_protocol_toyota_v0;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}



void subghz_protocol_decoder_toyota_v0_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderToyotaV0* instance = context;
    instance->decoder.parser_step = ToyotaV0DecoderStepReset;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->preamble_count = 0;
    instance->half_bit = false;
    instance->bit_count = 0;
    instance->data_32 = 0;
}

/**
 * Validate and emit a 88-bit frame.
 *
 * Checks complement: bit[i] XOR bit[i+88] == 1 for all i in [0,87].
 * Extracts 88 data bits (11 bytes) into generic.data (64 bits) + data_32 (24 bits).
 */
static void toyota_v0_check_frame(SubGhzProtocolDecoderToyotaV0* instance) {
    if(instance->bit_count < TOYOTA_V0_FRAME_BITS) return;

    // Validate complement at bit level
    // After trigger, bit positions: [0..87] = data, [88..95] = zero-gap, [96..183] = complement
    // Complement check: bit[i] XOR bit[i+96] must be 1 for data bits 0..87
    int errors = 0;
    for(int i = 0; i < TOYOTA_V0_DATA_BITS; i++) {
        if((instance->all_bits[i] ^ instance->all_bits[i + 96]) != 1) {
            errors++;
        }
    }

    if(errors > 2) {
        FURI_LOG_D(TAG, "Complement failed: %d errors in %d bits", errors, TOYOTA_V0_DATA_BITS);
        return;
    }

    // Extract generic.data (first 64 bits)
    instance->generic.data = 0;
    for(int i = 0; i < 64; i++) {
        instance->generic.data = (instance->generic.data << 1) | instance->all_bits[i];
    }

    // Extract data_32 (bits 64-87, left-aligned in 32 bits)
    instance->data_32 = 0;
    for(int i = 64; i < TOYOTA_V0_DATA_BITS; i++) {
        instance->data_32 = (instance->data_32 << 1) | instance->all_bits[i];
    }
    instance->data_32 <<= (32 - (TOYOTA_V0_DATA_BITS - 64)); // left-align: 24 bits → shift by 8

    instance->generic.data_count_bit = TOYOTA_V0_DATA_BITS;
    instance->decoder.decode_data = instance->generic.data;
    instance->decoder.decode_count_bit = TOYOTA_V0_DATA_BITS;

    uint32_t key_hi = (uint32_t)(instance->generic.data >> 32);
    uint32_t key_lo = (uint32_t)(instance->generic.data & 0xFFFFFFFF);

    FURI_LOG_I(TAG, "Decoded 88-bit: Key=%08lX%08lX Key2=%08lX",
        (unsigned long)key_hi, (unsigned long)key_lo,
        (unsigned long)instance->data_32);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

void subghz_protocol_decoder_toyota_v0_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderToyotaV0* instance = context;

    uint32_t te_s = subghz_protocol_toyota_v0_const.te_short;
    uint32_t te_l = subghz_protocol_toyota_v0_const.te_long;
    uint32_t te_d = subghz_protocol_toyota_v0_const.te_delta;

    switch(instance->decoder.parser_step) {
    case ToyotaV0DecoderStepReset:
        // Any short pulse starts preamble counting
        if(DURATION_DIFF(duration, te_s) <= te_d) {
            instance->preamble_count = 1;
            instance->decoder.parser_step = ToyotaV0DecoderStepPreamble;
        }
        break;

    case ToyotaV0DecoderStepPreamble:
        if(DURATION_DIFF(duration, te_s) <= te_d) {
            instance->preamble_count++;
        } else if(
            DURATION_DIFF(duration, te_l) <= te_d &&
            instance->preamble_count >= TOYOTA_V0_MIN_PREAMBLE) {
            // First LONG after preamble = trigger, this IS the first data bit
            instance->decoder.parser_step = ToyotaV0DecoderStepData;
            instance->bit_count = 0;
            instance->half_bit = false;

            // Record trigger LONG as first data bit (value = level)
            instance->all_bits[instance->bit_count++] = level ? 1 : 0;
        } else {
            // Not short and not valid trigger → reset
            instance->decoder.parser_step = ToyotaV0DecoderStepReset;
            instance->preamble_count = 0;
        }
        break;

    case ToyotaV0DecoderStepData: {
        bool is_short = DURATION_DIFF(duration, te_s) <= te_d;
        bool is_long = DURATION_DIFF(duration, te_l) <= te_d;

        if(is_long) {
            if(instance->half_bit) {
                // LONG after half-bit = Manchester error
                // Check if we have enough bits for a frame
                toyota_v0_check_frame(instance);
                instance->decoder.parser_step = ToyotaV0DecoderStepReset;
                instance->preamble_count = 0;
            } else {
                instance->all_bits[instance->bit_count++] = level ? 1 : 0;
            }
        } else if(is_short) {
            if(instance->half_bit) {
                instance->all_bits[instance->bit_count++] = level ? 1 : 0;
                instance->half_bit = false;
            } else {
                instance->half_bit = true;
            }
        } else {
            // Invalid timing → check frame & reset
            toyota_v0_check_frame(instance);
            instance->decoder.parser_step = ToyotaV0DecoderStepReset;
            instance->preamble_count = 0;
        }

        // Check if we have a complete frame
        if(instance->bit_count >= TOYOTA_V0_FRAME_BITS) {
            toyota_v0_check_frame(instance);
            // Reset to preamble detection for next frame
            instance->decoder.parser_step = ToyotaV0DecoderStepReset;
            instance->preamble_count = 0;
        }
    } break;

    default:
        instance->decoder.parser_step = ToyotaV0DecoderStepReset;
        break;
    }
}

// ============================================================================
// HASH
// ============================================================================



// ============================================================================
// SERIALIZATION
// ============================================================================

SubGhzProtocolStatus subghz_protocol_decoder_toyota_v0_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderToyotaV0* instance = context;

    SubGhzProtocolStatus res =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(res != SubGhzProtocolStatusOk) return res;

    uint32_t k2 = instance->data_32;
    if(!flipper_format_rewind(flipper_format)) return SubGhzProtocolStatusErrorParserOthers;
    if(!flipper_format_insert_or_update_uint32(flipper_format, "Key2", &k2, 1))
        return SubGhzProtocolStatusErrorParserOthers;

    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_toyota_v0_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderToyotaV0* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_deserialize(&instance->generic, flipper_format);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t k2 = 0;
    if(!flipper_format_read_uint32(flipper_format, "Key2", &k2, 1))
        return SubGhzProtocolStatusError;

    instance->data_32 = k2;
    instance->generic.protocol_name = instance->base.protocol->name;

    return SubGhzProtocolStatusOk;
}

// ============================================================================
// DISPLAY
// ============================================================================

// ============================================================================
// ENCODER
// ============================================================================

/**
 * Build one complete frame:
 * [PREAMBLE: 20x SHORT pairs] [96 Manchester data bits] [96 Manchester complement bits] [END GAP]
 */
