#include "nero_radio.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "common.h"
#include "../encode_plugin.h"

#define TAG "SubGhzProtocolNeroRadio"

static const SubGhzBlockConst subghz_protocol_nero_radio_const = {
    .te_short = 200,
    .te_long = 400,
    .te_delta = 80,
    .min_count_bit_for_found = 56,
};

// "Rolling long range" (manual name) format: ~128 ms carrier, 34 x 200/200 preamble, 430/1240 sync,
// then 251 NRZ bits at 4800 baud: 11-bit header, 4 fixed sync words and 11 data words.
// Every data word is one of 64 balanced 16-chip codes, so it carries 6 bits.
// All 66 data bits change on each press (even the same button), so it can only be replayed.
// Stored as Key = symbols 0..9 (6 bits each), Data = symbol 10.
#define NERO_RADIO_DYN_COUNT_BIT   66
#define NERO_RADIO_DYN_TE          208
#define NERO_RADIO_DYN_FRAME_BITS  251
#define NERO_RADIO_DYN_HEADER      0x54C // 10101001100
#define NERO_RADIO_DYN_HEADER_BITS 11
#define NERO_RADIO_DYN_SYNC_WORDS  4
#define NERO_RADIO_DYN_DATA_WORDS  11
#define NERO_RADIO_DYN_PREAMBLE    34
#define NERO_RADIO_DYN_CARRIER     128700
#define NERO_RADIO_DYN_GAP         3000

static const uint16_t nero_radio_dyn_sync[NERO_RADIO_DYN_SYNC_WORDS] = {
    0x896C,
    0x7435,
    0xCF7C,
    0x117B,
};

// Data alphabet (0x7435 only shows up as a sync word). Sorted, so the symbol value is
// just the index; the real value mapping is unknown
static const uint16_t nero_radio_dyn_symbols[64] = {
    0x117B, 0x12BD, 0x13C7, 0x14F6, 0x172E, 0x1D65, 0x1D8B, 0x1E53, 0x1ECC, 0x22DE, 0x236D,
    0x2597, 0x25EA, 0x26F1, 0x29BC, 0x2E1D, 0x2E66, 0x33B2, 0x344F, 0x3754, 0x38D5, 0x3A2B,
    0x3C78, 0x43AB, 0x44DB, 0x453D, 0x4772, 0x4AF4, 0x4DA6, 0x4E69, 0x519E, 0x5791, 0x58B3,
    0x59E8, 0x5B0D, 0x66AC, 0x691B, 0x6A87, 0x6DD0, 0x7127, 0x7259, 0x76C2, 0x85CD, 0x86A7,
    0x871B, 0x8B6A, 0x8DB1, 0x98DA, 0x9AE1, 0x9B94, 0xA176, 0xA9C3, 0xAC8E, 0xB0EC, 0xB11D,
    0xB433, 0xB689, 0xC24F, 0xC89D, 0xCE16, 0xD23A, 0xD364, 0xD455, 0xD54A,
};

// "Normal rolling" (manual name) format, same 4800 baud chips:
// ~128 ms carrier, 8 x 200/200, 400 low, 1250 high, 52 fixed chips (same on every press,
// maybe the remote ID), 1260 low, 47 x 200/200, 830 low, then 251 chips:
// 1110 + 00100100 sync, 16 Hamming(7,4) words Manchester coded (1 = 10, 0 = 01) = 64 bits,
// then 2 more Hamming(7,4) words sent as raw chips with a 0 chip between them.
// The tail always repeats the last key byte. The high nibbles of bytes 1..6 depend only on
// the high nibble of byte 0 (16 entry table, h(n ^ D) = h(n) ^ DDDDDD on one remote), so only
// ~40 of the 64 bits carry the rolling part. The button changes the high nibbles of bytes 5..6.
// The last byte follows a press counter: counter bit 0 flipping always XORs it with 0x5F,
// bits 0..1 with 0x69, bits 0..3 and up with 0x14. The algorithm is still unknown, so it can
// only be replayed. Stored as Key = 64 bits, Data = 52 fixed chips << 8 | tail.
#define NERO_RADIO_NORM_COUNT_BIT  72
#define NERO_RADIO_NORM_HEADER     0xE24 // 111000100100
#define NERO_RADIO_NORM_HEAD_BITS  12
#define NERO_RADIO_NORM_TAIL_START (NERO_RADIO_NORM_HEAD_BITS + 16 * 7 * 2)
#define NERO_RADIO_NORM_ID_BITS    52
#define NERO_RADIO_NORM_ID_PREAM   8
#define NERO_RADIO_NORM_PREAMBLE   47
// A valid raw tail plus the last Manchester chip can contain 12 consecutive lows.
#define NERO_RADIO_NORM_GAP_MIN    (13 * NERO_RADIO_DYN_TE)
#define NERO_RADIO_UPLOAD_CAPACITY 440

struct SubGhzProtocolDecoderNeroRadio {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t header_count;
    uint16_t dyn_bit;
    uint16_t dyn_word;
    uint8_t dyn_last;
    bool norm_id_ok;
    uint64_t norm_id;
    uint8_t norm_word;
    uint8_t norm_tail;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderNeroRadio);

struct SubGhzProtocolEncoderNeroRadio {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderNeroRadio);

typedef enum {
    NeroRadioDecoderStepReset = 0,
    NeroRadioDecoderStepCheckPreambula,
    NeroRadioDecoderStepSaveDuration,
    NeroRadioDecoderStepCheckDuration,
    NeroRadioDecoderStepDynData,
    NeroRadioDecoderStepNormIdSync,
    NeroRadioDecoderStepNormId,
    NeroRadioDecoderStepNormData,
} NeroRadioDecoderStep;

/**
 * Hamming(7,4) codeword of a nibble, data bits first.
 * @param nibble Data
 * @return 7-bit codeword
 */
static uint8_t subghz_protocol_nero_radio_hamming(uint8_t nibble) {
    static const uint8_t parity[4] = {0x7, 0x6, 0x5, 0x3}; // for data bits 0..3
    uint8_t par = 0;
    for(uint8_t i = 0; i < 4; i++) {
        if(nibble & (1 << i)) par ^= parity[i];
    }
    return (nibble << 3) | par;
}

static void subghz_protocol_decoder_nero_radio_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderNeroRadio* instance = context;
    subghz_protocol_decoder_common_reset(context);
    instance->norm_id_ok = false;
}

static SubGhzProtocolStatus subghz_protocol_decoder_nero_radio_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

const SubGhzProtocolDecoder subghz_protocol_nero_radio_decoder = {
    .alloc = subghz_protocol_decoder_nero_radio_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_nero_radio_feed,
    .reset = subghz_protocol_decoder_nero_radio_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_nero_radio_serialize,
    .deserialize = subghz_protocol_decoder_nero_radio_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_nero_radio_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_nero_radio");

const SubGhzProtocol subghz_protocol_nero_radio = {
    .name = SUBGHZ_PROTOCOL_NERO_RADIO_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_nero_radio_decoder,
    .encoder = &subghz_protocol_nero_radio_encoder_ext.vtable,
};

typedef struct {
    LevelDuration* upload;
    size_t index;
    bool level;
    uint32_t run;
} NeroRadioChipWriter;

/**
 * Generating an upload for the rolling long range format.
 * @param instance Pointer to a SubGhzProtocolEncoderNeroRadio instance
 * @return true On success
 */
/**
 * Generating an upload for the normal rolling format.
 * @param instance Pointer to a SubGhzProtocolEncoderNeroRadio instance
 * @return true On success
 */
/**
 * Generating an upload from data.
 * @param instance Pointer to a SubGhzProtocolEncoderNeroRadio instance
 * @return true On success
 */
/**
 * Deserialize the key, accepting 56/57 bit static and 66/72 bit rolling replay formats.
 * @param generic Pointer to a SubGhzBlockGeneric instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @return status
 */
static SubGhzProtocolStatus subghz_protocol_nero_radio_deserialize_key(
    SubGhzBlockGeneric* generic,
    FlipperFormat* flipper_format) {
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        generic, flipper_format, subghz_protocol_nero_radio_const.min_count_bit_for_found);
    if(ret != SubGhzProtocolStatusErrorValueBitCount) {
        return ret;
    }
    if(generic->data_count_bit == 57) {
        return SubGhzProtocolStatusOk;
    }
    if((generic->data_count_bit != NERO_RADIO_DYN_COUNT_BIT) &&
       (generic->data_count_bit != NERO_RADIO_NORM_COUNT_BIT)) {
        return ret;
    }

    uint8_t key_data[sizeof(uint64_t)] = {0};
    if(!flipper_format_rewind(flipper_format) ||
       !flipper_format_read_hex(flipper_format, "Data", key_data, sizeof(uint64_t))) {
        FURI_LOG_E(TAG, "Missing Data");
        return SubGhzProtocolStatusErrorParserOthers;
    }
    generic->data_2 = 0;
    for(uint8_t i = 0; i < sizeof(uint64_t); i++) {
        generic->data_2 = generic->data_2 << 8 | key_data[i];
    }
    return SubGhzProtocolStatusOk;
}

void* subghz_protocol_decoder_nero_radio_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderNeroRadio), &subghz_protocol_nero_radio);
}

/**
 * Add one NRZ chip of the dynamic format, checking header and sync words.
 * @param instance Pointer to a SubGhzProtocolDecoderNeroRadio instance
 * @param bit Chip value
 * @return false if the frame does not match
 */
static bool
    subghz_protocol_nero_radio_dyn_add_bit(SubGhzProtocolDecoderNeroRadio* instance, bool bit) {
    uint16_t pos = instance->dyn_bit++;
    if(pos >= NERO_RADIO_DYN_FRAME_BITS) return false;
    if(pos < NERO_RADIO_DYN_HEADER_BITS) {
        return bit == ((NERO_RADIO_DYN_HEADER >> (NERO_RADIO_DYN_HEADER_BITS - 1 - pos)) & 1);
    }

    pos -= NERO_RADIO_DYN_HEADER_BITS;
    instance->dyn_word = (instance->dyn_word << 1) | bit;
    if((pos & 0xF) != 0xF) return true;

    uint8_t word = pos >> 4;
    if(word < NERO_RADIO_DYN_SYNC_WORDS) {
        return instance->dyn_word == nero_radio_dyn_sync[word];
    }
    for(uint8_t i = 0; i < COUNT_OF(nero_radio_dyn_symbols); i++) {
        if(nero_radio_dyn_symbols[i] != instance->dyn_word) continue;
        if(word == NERO_RADIO_DYN_SYNC_WORDS + NERO_RADIO_DYN_DATA_WORDS - 1) {
            instance->dyn_last = i;
        } else {
            instance->decoder.decode_data = (instance->decoder.decode_data << 6) | i;
        }
        return true;
    }
    return false;
}

/**
 * Add one chip of the normal rolling format: header, Manchester coded Hamming words, raw tail.
 * @param instance Pointer to a SubGhzProtocolDecoderNeroRadio instance
 * @param bit Chip value
 * @return false if the frame does not match
 */
static bool
    subghz_protocol_nero_radio_norm_add_bit(SubGhzProtocolDecoderNeroRadio* instance, bool bit) {
    uint16_t pos = instance->dyn_bit++;
    if(pos >= NERO_RADIO_DYN_FRAME_BITS) return false;
    if(pos < NERO_RADIO_NORM_HEAD_BITS) {
        return bit == ((NERO_RADIO_NORM_HEADER >> (NERO_RADIO_NORM_HEAD_BITS - 1 - pos)) & 1);
    }

    uint16_t word_end;
    if(pos < NERO_RADIO_NORM_TAIL_START) {
        uint16_t rel = pos - NERO_RADIO_NORM_HEAD_BITS;
        if(!(rel & 1)) {
            instance->dyn_word = bit;
            return true;
        }
        if(instance->dyn_word == bit) return false; // not Manchester
        instance->norm_word = (instance->norm_word << 1) | instance->dyn_word;
        word_end = (rel >> 1) % 7 == 6;
    } else {
        uint16_t rel = pos - NERO_RADIO_NORM_TAIL_START;
        if(rel == 7) return !bit;
        instance->norm_word = (instance->norm_word << 1) | bit;
        word_end = (rel == 6) || (rel == 14);
    }
    if(!word_end) return true;

    for(uint8_t nibble = 0; nibble < 16; nibble++) {
        if(subghz_protocol_nero_radio_hamming(nibble) != (instance->norm_word & 0x7F)) continue;
        if(pos < NERO_RADIO_NORM_TAIL_START) {
            instance->decoder.decode_data = (instance->decoder.decode_data << 4) | nibble;
        } else {
            instance->norm_tail = (instance->norm_tail << 4) | nibble;
        }
        return true;
    }
    return false;
}

void subghz_protocol_decoder_nero_radio_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderNeroRadio* instance = context;

    switch(instance->decoder.parser_step) {
    case NeroRadioDecoderStepReset:
        if((level) && (DURATION_DIFF(duration, subghz_protocol_nero_radio_const.te_short) <
                       subghz_protocol_nero_radio_const.te_delta)) {
            instance->decoder.parser_step = NeroRadioDecoderStepCheckPreambula;
            instance->decoder.te_last = duration;
            instance->header_count = 0;
        }
        break;
    case NeroRadioDecoderStepCheckPreambula:
        if(level) {
            if((DURATION_DIFF(duration, subghz_protocol_nero_radio_const.te_short) <
                subghz_protocol_nero_radio_const.te_delta) ||
               (DURATION_DIFF(duration, subghz_protocol_nero_radio_const.te_short * 4) <
                subghz_protocol_nero_radio_const.te_delta) ||
               (DURATION_DIFF(duration, subghz_protocol_nero_radio_const.te_long) <
                subghz_protocol_nero_radio_const.te_delta)) {
                instance->decoder.te_last = duration;
            } else {
                instance->decoder.parser_step = NeroRadioDecoderStepReset;
            }
        } else if(
            (DURATION_DIFF(duration, 1240) < 200) &&
            (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_nero_radio_const.te_long) <
             subghz_protocol_nero_radio_const.te_delta)) {
            // Found dynamic format sync
            if(instance->header_count > 25) {
                instance->decoder.parser_step = NeroRadioDecoderStepDynData;
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->dyn_bit = 0;
                instance->dyn_word = 0;
            } else {
                instance->decoder.parser_step = NeroRadioDecoderStepReset;
            }
        } else if(
            (DURATION_DIFF(duration, subghz_protocol_nero_radio_const.te_long) <
             subghz_protocol_nero_radio_const.te_delta) &&
            (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_nero_radio_const.te_short) <
             subghz_protocol_nero_radio_const.te_delta) &&
            (instance->header_count >= NERO_RADIO_NORM_ID_PREAM - 3)) {
            // Found normal rolling id block sync, 1250 us high follows
            instance->decoder.parser_step = NeroRadioDecoderStepNormIdSync;
        } else if(
            (DURATION_DIFF(duration, 830) < 120) &&
            (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_nero_radio_const.te_short) <
             subghz_protocol_nero_radio_const.te_delta) &&
            (instance->header_count > 40)) {
            // Found normal rolling start
            instance->decoder.parser_step = NeroRadioDecoderStepNormData;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->dyn_bit = 0;
            instance->norm_word = 0;
            instance->norm_tail = 0;
        } else if(
            DURATION_DIFF(duration, subghz_protocol_nero_radio_const.te_short) <
            subghz_protocol_nero_radio_const.te_delta) {
            if(DURATION_DIFF(instance->decoder.te_last, subghz_protocol_nero_radio_const.te_short) <
               subghz_protocol_nero_radio_const.te_delta) {
                // Found header
                instance->header_count++;
                break;
            } else if(
                DURATION_DIFF(
                    instance->decoder.te_last, subghz_protocol_nero_radio_const.te_short * 4) <
                subghz_protocol_nero_radio_const.te_delta) {
                // Found start bit
                if(instance->header_count > 40) {
                    instance->decoder.parser_step = NeroRadioDecoderStepSaveDuration;
                    instance->decoder.decode_data = 0;
                    instance->decoder.decode_count_bit = 0;
                } else {
                    instance->decoder.parser_step = NeroRadioDecoderStepReset;
                }
            } else {
                instance->decoder.parser_step = NeroRadioDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = NeroRadioDecoderStepReset;
        }
        break;
    case NeroRadioDecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = NeroRadioDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = NeroRadioDecoderStepReset;
        }
        break;
    case NeroRadioDecoderStepCheckDuration:
        if(!level) {
            // Data lows are 200/400 us, frames end with 1.0..1.3 ms or a longer gap
            if(duration >= ((uint32_t)900)) {
                //Found stop bit
                if(DURATION_DIFF(
                       instance->decoder.te_last, subghz_protocol_nero_radio_const.te_short) <
                   subghz_protocol_nero_radio_const.te_delta) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                } else if(
                    DURATION_DIFF(
                        instance->decoder.te_last, subghz_protocol_nero_radio_const.te_long) <
                    subghz_protocol_nero_radio_const.te_delta) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                }
                instance->decoder.parser_step = NeroRadioDecoderStepReset;
                if((instance->decoder.decode_count_bit ==
                    subghz_protocol_nero_radio_const.min_count_bit_for_found) ||
                   (instance->decoder.decode_count_bit ==
                    subghz_protocol_nero_radio_const.min_count_bit_for_found + 1)) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->decoder.parser_step = NeroRadioDecoderStepReset; //-V1048
                break;
            } else if(
                (DURATION_DIFF(
                     instance->decoder.te_last, subghz_protocol_nero_radio_const.te_short) <
                 subghz_protocol_nero_radio_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_nero_radio_const.te_long) <
                 subghz_protocol_nero_radio_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = NeroRadioDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(
                     instance->decoder.te_last, subghz_protocol_nero_radio_const.te_long) <
                 subghz_protocol_nero_radio_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_nero_radio_const.te_short) <
                 subghz_protocol_nero_radio_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = NeroRadioDecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = NeroRadioDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = NeroRadioDecoderStepReset;
        }
        break;
    case NeroRadioDecoderStepDynData:
        if(!level && (duration >= 1500)) {
            // Gap: trailing zero chips of the last word merge into it
            bool valid = (NERO_RADIO_DYN_FRAME_BITS - instance->dyn_bit) <= 8;
            while(valid && instance->dyn_bit < NERO_RADIO_DYN_FRAME_BITS) {
                valid = subghz_protocol_nero_radio_dyn_add_bit(instance, false);
            }
            if(valid) {
                instance->generic.data = instance->decoder.decode_data;
                instance->generic.data_2 = instance->dyn_last;
                instance->generic.data_count_bit = NERO_RADIO_DYN_COUNT_BIT;
                instance->decoder.decode_count_bit = NERO_RADIO_DYN_COUNT_BIT;

                if(instance->base.callback)
                    instance->base.callback(&instance->base, instance->base.context);
            }
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = NeroRadioDecoderStepReset;
        } else {
            uint32_t count = (duration + NERO_RADIO_DYN_TE / 2) / NERO_RADIO_DYN_TE;
            if((count == 0) || (count > 7)) {
                instance->decoder.parser_step = NeroRadioDecoderStepReset;
                break;
            }
            for(uint32_t i = 0; i < count; i++) {
                if(!subghz_protocol_nero_radio_dyn_add_bit(instance, level)) {
                    instance->decoder.parser_step = NeroRadioDecoderStepReset;
                    break;
                }
            }
        }
        break;
    case NeroRadioDecoderStepNormIdSync:
        if(level && (DURATION_DIFF(duration, 1250) < 200)) {
            instance->decoder.parser_step = NeroRadioDecoderStepNormId;
            instance->dyn_bit = 0;
            instance->norm_id = 0;
            instance->norm_id_ok = false;
        } else {
            instance->decoder.parser_step = NeroRadioDecoderStepReset;
        }
        break;
    case NeroRadioDecoderStepNormId:
        if(!level && (duration >= 1000)) {
            // Low before the main preamble ends the id block
            instance->norm_id_ok = (instance->dyn_bit == NERO_RADIO_NORM_ID_BITS);
            instance->decoder.parser_step = NeroRadioDecoderStepReset;
        } else {
            uint32_t count = (duration + NERO_RADIO_DYN_TE / 2) / NERO_RADIO_DYN_TE;
            instance->dyn_bit += count;
            if((count == 0) || (count > 7) || (instance->dyn_bit > NERO_RADIO_NORM_ID_BITS)) {
                instance->decoder.parser_step = NeroRadioDecoderStepReset;
                break;
            }
            for(uint32_t i = 0; i < count; i++) {
                instance->norm_id = (instance->norm_id << 1) | level;
            }
        }
        break;
    case NeroRadioDecoderStepNormData:
        if(!level && (duration >= NERO_RADIO_NORM_GAP_MIN)) {
            // Gap: trailing zero chips of the tail merge into it
            bool valid = instance->norm_id_ok &&
                         (NERO_RADIO_DYN_FRAME_BITS - instance->dyn_bit) <= 16;
            while(valid && instance->dyn_bit < NERO_RADIO_DYN_FRAME_BITS) {
                valid = subghz_protocol_nero_radio_norm_add_bit(instance, false);
            }
            // The raw tail repeats the last key byte
            valid = valid && (instance->norm_tail == (instance->decoder.decode_data & 0xFF));
            if(valid) {
                instance->generic.data = instance->decoder.decode_data;
                instance->generic.data_2 = (instance->norm_id << 8) | instance->norm_tail;
                instance->generic.data_count_bit = NERO_RADIO_NORM_COUNT_BIT;
                instance->decoder.decode_count_bit = NERO_RADIO_NORM_COUNT_BIT;

                if(instance->base.callback)
                    instance->base.callback(&instance->base, instance->base.context);
            }
            instance->norm_id_ok = false;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = NeroRadioDecoderStepReset;
        } else {
            uint32_t count = (duration + NERO_RADIO_DYN_TE / 2) / NERO_RADIO_DYN_TE;
            if((count == 0) || (count > 16)) {
                instance->norm_id_ok = false;
                instance->decoder.parser_step = NeroRadioDecoderStepReset;
                break;
            }
            for(uint32_t i = 0; i < count; i++) {
                if(!subghz_protocol_nero_radio_norm_add_bit(instance, level)) {
                    instance->norm_id_ok = false;
                    instance->decoder.parser_step = NeroRadioDecoderStepReset;
                    break;
                }
            }
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_nero_radio_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderNeroRadio* instance = context;
    return subghz_protocol_nero_radio_deserialize_key(&instance->generic, flipper_format);
}

static SubGhzProtocolStatus subghz_protocol_decoder_nero_radio_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderNeroRadio* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if((instance->generic.data_count_bit == NERO_RADIO_DYN_COUNT_BIT) ||
       (instance->generic.data_count_bit == NERO_RADIO_NORM_COUNT_BIT)) {
        ret = subghz_protocol_common_append_data_2(ret, &instance->generic, flipper_format);
    }
    return ret;
}


/** 
 * Analysis of received data
 * @param instance Pointer to a SubGhzBlockGeneric* instance
 */
