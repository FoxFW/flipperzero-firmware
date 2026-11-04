#include <furi.h>
#include "toolbox/level_duration.h"
#include "protocol_gproxii.h"
#include <toolbox/manchester_decoder.h>
#include <bit_lib/bit_lib.h>
#include "lfrfid_protocols.h"

#define GPROXII_PREAMBLE_BIT_SIZE (6)
#define GPROXII_ENCODED_BIT_SIZE  (90)
#define GPROXII_ENCODED_BYTE_FULL_SIZE \
    (((GPROXII_PREAMBLE_BIT_SIZE + GPROXII_ENCODED_BIT_SIZE) / 8))

#define GPROXII_DATA_SIZE (12)

#define GPROXII_SHORT_TIME  (256)
#define GPROXII_LONG_TIME   (512)
#define GPROXII_JITTER_TIME (120)

#define GPROXII_SHORT_TIME_LOW  (GPROXII_SHORT_TIME - GPROXII_JITTER_TIME)
#define GPROXII_SHORT_TIME_HIGH (GPROXII_SHORT_TIME + GPROXII_JITTER_TIME)
#define GPROXII_LONG_TIME_LOW   (GPROXII_LONG_TIME - GPROXII_JITTER_TIME)
#define GPROXII_LONG_TIME_HIGH  (GPROXII_LONG_TIME + GPROXII_JITTER_TIME)

typedef struct {
    bool last_short;
    bool last_level;
    size_t encoded_index;
    uint8_t decoded_data[GPROXII_ENCODED_BYTE_FULL_SIZE];
    uint8_t data[GPROXII_ENCODED_BYTE_FULL_SIZE];
} ProtocolGProxII;

ProtocolGProxII* protocol_gproxii_alloc(void) {
    ProtocolGProxII* protocol = malloc(sizeof(ProtocolGProxII));
    return protocol;
}

void protocol_gproxii_free(ProtocolGProxII* protocol) {
    free(protocol);
}

uint8_t* protocol_gproxii_get_data(ProtocolGProxII* protocol) {
    return protocol->data;
}

bool wiegand_check(uint64_t fc_and_card, bool even_parity, bool odd_parity, int card_len) {
    uint8_t even_parity_sum = 0;
    uint8_t odd_parity_sum = 1;
    switch(card_len) {
    case 26:
        for(int8_t i = 12; i < 24; i++) {
            if(((fc_and_card >> i) & 1) == 1) {
                even_parity_sum++;
            }
        }
        if(even_parity_sum % 2 != even_parity) return false;

        for(int8_t i = 0; i < 12; i++) {
            if(((fc_and_card >> i) & 1) == 1) {
                odd_parity_sum++;
            }
        }
        if(odd_parity_sum % 2 != odd_parity) return false;
        break;
    case 36:
        for(int8_t i = 17; i < 34; i++) {
            if(((fc_and_card >> i) & 1) == 1) {
                even_parity_sum++;
            }
        }
        if(even_parity_sum % 2 != even_parity) return false;

        for(int8_t i = 0; i < 17; i++) {
            if(((fc_and_card >> i) & 1) == 1) {
                odd_parity_sum++;
            }
        }
        if(odd_parity_sum % 2 != odd_parity) return false;
        break;
    default:
        furi_crash();
    }
    return true;
}

void protocol_gproxii_decoder_start(ProtocolGProxII* protocol) {
    memset(protocol->data, 0, GPROXII_ENCODED_BYTE_FULL_SIZE);
    memset(protocol->decoded_data, 0, GPROXII_DATA_SIZE);
    protocol->last_short = false;
}

static bool protocol_gproxii_can_be_decoded(ProtocolGProxII* protocol) {

    if(bit_lib_get_bits(protocol->data, 0, 6) != 0b111110) return false;

    if(!bit_lib_test_parity(protocol->data, 6, GPROXII_ENCODED_BIT_SIZE, BitLibParityAlways0, 5)) {
        return false;
    }

    bit_lib_copy_bits(protocol->decoded_data, 0, GPROXII_ENCODED_BIT_SIZE, protocol->data, 6);

    bit_lib_remove_bit_every_nth(protocol->decoded_data, 0, GPROXII_ENCODED_BIT_SIZE, 5);

    for(int i = 0; i < 9; i++) {
        protocol->decoded_data[i] = bit_lib_reverse_8_fast(protocol->decoded_data[i]);
    }

    for(int i = 1; i < 9; i++) {
        protocol->decoded_data[i] = protocol->decoded_data[0] ^ protocol->decoded_data[i];
    }

    int card_len = bit_lib_get_bits(protocol->decoded_data, 8, 6);

    if(card_len == 26) {
        uint64_t fc_and_card = bit_lib_get_bits_64(protocol->decoded_data, 33, 24);
        bool even_parity = bit_lib_get_bits(protocol->decoded_data, 32, 1);
        bool odd_parity = bit_lib_get_bits(protocol->decoded_data, 57, 1);
        if(!wiegand_check(fc_and_card, even_parity, odd_parity, card_len)) return false;
    } else if(card_len == 36) {
        uint64_t fc_and_card = bit_lib_get_bits_64(protocol->decoded_data, 33, 34);
        uint8_t even_parity = bit_lib_get_bits(protocol->decoded_data, 32, 1);
        uint8_t odd_parity = bit_lib_get_bits(protocol->decoded_data, 67, 1);
        if(!wiegand_check(fc_and_card, even_parity, odd_parity, card_len)) return false;
    } else {
        return false;
    }

    return true;
}

bool protocol_gproxii_decoder_feed(ProtocolGProxII* protocol, bool level, uint32_t duration) {
    UNUSED(level);
    bool pushed = false;

    if(duration >= GPROXII_SHORT_TIME_LOW && duration <= GPROXII_SHORT_TIME_HIGH) {
        if(protocol->last_short == false) {
            protocol->last_short = true;
        } else {
            pushed = true;
            bit_lib_push_bit(protocol->data, GPROXII_ENCODED_BYTE_FULL_SIZE, true);
            protocol->last_short = false;
        }
    } else if(duration >= GPROXII_LONG_TIME_LOW && duration <= GPROXII_LONG_TIME_HIGH) {
        if(protocol->last_short == false) {
            pushed = true;
            bit_lib_push_bit(protocol->data, GPROXII_ENCODED_BYTE_FULL_SIZE, false);
        } else {

            protocol->last_short = false;
        }
    } else {

        protocol->last_short = false;
    }

    if(pushed && protocol_gproxii_can_be_decoded(protocol)) {
        return true;
    }

    return false;
}

bool protocol_gproxii_encoder_start(ProtocolGProxII* protocol) {
    protocol->encoded_index = 0;
    protocol->last_short = false;
    protocol->last_level = false;
    return true;
}

LevelDuration protocol_gproxii_encoder_yield(ProtocolGProxII* protocol) {
    uint32_t duration;
    protocol->last_level = !protocol->last_level;

    bool bit = bit_lib_get_bit(protocol->data, protocol->encoded_index);

    if(bit) {

        duration = GPROXII_SHORT_TIME / 8;
        if(protocol->last_short) {
            bit_lib_increment_index(protocol->encoded_index, 96);
            protocol->last_short = false;
        } else {
            protocol->last_short = true;
        }
    } else {

        duration = GPROXII_LONG_TIME / 8;
        bit_lib_increment_index(protocol->encoded_index, 96);
    }
    return level_duration_make(protocol->last_level, duration);
}

void protocol_gproxii_render_data(ProtocolGProxII* protocol, FuriString* result) {
    protocol_gproxii_can_be_decoded(protocol);
    int xor_code = bit_lib_get_bits(protocol->decoded_data, 0, 8);
    int card_len = bit_lib_get_bits(protocol->decoded_data, 8, 6);
    int crc_code = bit_lib_get_bits(protocol->decoded_data, 14, 2);

    if(card_len == 26) {

        furi_string_cat_printf(
            result,
            "FC: %u Card: %u LEN: %hhu\n",
            bit_lib_get_bits(protocol->decoded_data, 33, 8),
            bit_lib_get_bits_16(protocol->decoded_data, 41, 16),
            card_len);

        furi_string_cat_printf(
            result,
            "XOR: %hhu CRC: %hhu P: %04hX",
            xor_code,
            crc_code,
            bit_lib_get_bits_16(protocol->decoded_data, 16, 16));
    } else if(card_len == 36) {

        furi_string_cat_printf(
            result,
            "FC: %u Card: %u LEN: %hhu\n",
            bit_lib_get_bits_16(protocol->decoded_data, 33, 14),
            bit_lib_get_bits_16(protocol->decoded_data, 51, 16),
            card_len);

        furi_string_cat_printf(
            result,
            "XOR: %hhu CRC: %hhu P: %04hX",
            xor_code,
            crc_code,
            bit_lib_get_bits_16(protocol->decoded_data, 16, 16));
    } else {
        furi_string_cat_printf(result, "Read Error\n");
    }
}

bool protocol_gproxii_write_data(ProtocolGProxII* protocol, void* data) {
    LFRFIDWriteRequest* request = (LFRFIDWriteRequest*)data;
    bool result = false;

    if(request->write_type == LFRFIDWriteTypeT5577) {
        request->t5577.block[0] = LFRFID_T5577_MODULATION_BIPHASE | LFRFID_T5577_BITRATE_RF_64 |
                                  (3 << LFRFID_T5577_MAXBLOCK_SHIFT);
        request->t5577.block[1] = bit_lib_get_bits_32(protocol->data, 0, 32);
        request->t5577.block[2] = bit_lib_get_bits_32(protocol->data, 32, 32);
        request->t5577.block[3] = bit_lib_get_bits_32(protocol->data, 64, 32);
        request->t5577.blocks_to_write = 4;
        result = true;
    } else if(request->write_type == LFRFIDWriteTypeEM4305) {
        request->em4305.word[4] =
            (EM4x05_MODULATION_BIPHASE | EM4x05_SET_BITRATE(64) | (7 << EM4x05_MAXBLOCK_SHIFT));
        uint32_t encoded_data_reversed[3] = {0};
        for(uint8_t i = 0; i < 96; i++) {
            encoded_data_reversed[i / 32] = (encoded_data_reversed[i / 32] << 1) |
                                            (bit_lib_get_bit(protocol->data, (95 - i)) & 1);
            encoded_data_reversed[i / 32] ^= 1;
        }
        request->em4305.word[5] = encoded_data_reversed[2];
        request->em4305.word[6] = encoded_data_reversed[1];
        request->em4305.word[7] = encoded_data_reversed[0];
        request->em4305.mask = 0xF0;
        result = true;
    }
    return result;
}

const ProtocolBase protocol_gproxii = {
    .name = "GProxII",
    .manufacturer = "Guardall",
    .data_size = GPROXII_DATA_SIZE,
    .features = LFRFIDFeatureASK,
    .validate_count = 3,
    .alloc = (ProtocolAlloc)protocol_gproxii_alloc,
    .free = (ProtocolFree)protocol_gproxii_free,
    .get_data = (ProtocolGetData)protocol_gproxii_get_data,
    .decoder =
        {
            .start = (ProtocolDecoderStart)protocol_gproxii_decoder_start,
            .feed = (ProtocolDecoderFeed)protocol_gproxii_decoder_feed,
        },
    .encoder =
        {
            .start = (ProtocolEncoderStart)protocol_gproxii_encoder_start,
            .yield = (ProtocolEncoderYield)protocol_gproxii_encoder_yield,
        },
    .render_data = (ProtocolRenderData)protocol_gproxii_render_data,
    .render_brief_data = (ProtocolRenderData)protocol_gproxii_render_data,
    .write_data = (ProtocolWriteData)protocol_gproxii_write_data,
};
