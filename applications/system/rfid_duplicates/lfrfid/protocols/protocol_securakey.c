#include <furi.h>
#include <toolbox/protocols/protocol.h>
#include <toolbox/hex.h>
#include <bit_lib/bit_lib.h>
#include "lfrfid_protocols.h"
#include <toolbox/manchester_decoder.h>

#define TAG "SECURAKEY"

#define SECURAKEY_RKKT_ENCODED_FULL_SIZE_BITS (96)
#define SECURAKEY_RKKT_ENCODED_FULL_SIZE_BYTE (12)

#define SECURAKEY_RKKTH_ENCODED_FULL_SIZE_BITS (64)
#define SECURAKEY_RKKTH_ENCODED_FULL_SIZE_BYTE (8)

#define SECURAKEY_DECODED_DATA_SIZE_BITS  (48)

#define SECURAKEY_DECODED_DATA_SIZE_BYTES (SECURAKEY_DECODED_DATA_SIZE_BITS / 8)
#define LFRFID_FREQUENCY                  (125000)
#define SECURAKEY_CLOCK_PER_BIT           (40)
#define SECURAKEY_READ_LONG_TIME \
    (1000000 / (LFRFID_FREQUENCY / SECURAKEY_CLOCK_PER_BIT))
#define SECURAKEY_READ_SHORT_TIME  (SECURAKEY_READ_LONG_TIME / 2)
#define SECURAKEY_READ_JITTER_TIME (SECURAKEY_READ_SHORT_TIME * 40 / 100)
#define SECURAKEY_READ_SHORT_TIME_LOW \
    (SECURAKEY_READ_SHORT_TIME -      \
     SECURAKEY_READ_JITTER_TIME)
#define SECURAKEY_READ_SHORT_TIME_HIGH (SECURAKEY_READ_SHORT_TIME + SECURAKEY_READ_JITTER_TIME)
#define SECURAKEY_READ_LONG_TIME_LOW   (SECURAKEY_READ_LONG_TIME - SECURAKEY_READ_JITTER_TIME)
#define SECURAKEY_READ_LONG_TIME_HIGH  (SECURAKEY_READ_LONG_TIME + SECURAKEY_READ_JITTER_TIME)

typedef struct {
    uint8_t data[SECURAKEY_DECODED_DATA_SIZE_BYTES];
    uint8_t RKKT_encoded_data[SECURAKEY_RKKT_ENCODED_FULL_SIZE_BYTE];
    uint8_t RKKTH_encoded_data[SECURAKEY_RKKTH_ENCODED_FULL_SIZE_BYTE];
    uint8_t encoded_data_index;
    bool encoded_polarity;
    ManchesterState decoder_manchester_state;
    uint8_t bit_format;
} ProtocolSecurakey;

ProtocolSecurakey* protocol_securakey_alloc(void) {
    ProtocolSecurakey* protocol = malloc(sizeof(ProtocolSecurakey));
    return (void*)protocol;
}

void protocol_securakey_free(ProtocolSecurakey* protocol) {
    free(protocol);
}

uint8_t* protocol_securakey_get_data(ProtocolSecurakey* protocol) {
    return protocol->data;
}

static bool protocol_securakey_can_be_decoded(ProtocolSecurakey* protocol) {

    if(bit_lib_get_bits_32(protocol->RKKT_encoded_data, 0, 19) == 0b0111111111000000000) {
        if(bit_lib_test_parity(protocol->RKKT_encoded_data, 2, 54, BitLibParityAlways0, 9)) {
            protocol->bit_format = 0;
            return true;
        }
    } else if(bit_lib_get_bits_32(protocol->RKKT_encoded_data, 0, 19) == 0b0111111111001011010) {
        if(bit_lib_test_parity(protocol->RKKT_encoded_data, 2, 90, BitLibParityAlways0, 9)) {
            protocol->bit_format = 26;
            return true;
        }
    } else if(bit_lib_get_bits_32(protocol->RKKT_encoded_data, 0, 19) == 0b0111111111001100000) {
        if(bit_lib_test_parity(protocol->RKKT_encoded_data, 2, 90, BitLibParityAlways0, 9)) {
            protocol->bit_format = 32;
            return true;
        }
    }
    return false;
}

static void protocol_securakey_decode(ProtocolSecurakey* protocol) {
    memset(protocol->data, 0, SECURAKEY_DECODED_DATA_SIZE_BYTES);

    if(bit_lib_get_bits(protocol->RKKT_encoded_data, 13, 6) == 0) {
        FURI_LOG_D(TAG, "Plaintext RKKTH detected");
        protocol->bit_format = 0;

        bit_lib_copy_bits(protocol->data, 16, 8, protocol->RKKT_encoded_data, 29);

        bit_lib_copy_bits(protocol->data, 24, 8, protocol->RKKT_encoded_data, 38);
        bit_lib_copy_bits(protocol->data, 32, 8, protocol->RKKT_encoded_data, 47);
        bit_lib_copy_bits(protocol->data, 40, 8, protocol->RKKT_encoded_data, 56);
    } else {
        if(bit_lib_get_bits(protocol->RKKT_encoded_data, 13, 6) == 26) {
            FURI_LOG_D(TAG, "26-bit RKKT detected");
            protocol->bit_format = 26;

            bit_lib_copy_bits(protocol->data, 8, 1, protocol->RKKT_encoded_data, 36);

            bit_lib_copy_bits(protocol->data, 9, 7, protocol->RKKT_encoded_data, 38);
        } else if(bit_lib_get_bits(protocol->RKKT_encoded_data, 13, 6) == 32) {
            FURI_LOG_D(TAG, "32-bit RKKT detected");
            protocol->bit_format = 32;

            bit_lib_copy_bits(protocol->data, 2, 7, protocol->RKKT_encoded_data, 30);

            bit_lib_copy_bits(protocol->data, 9, 7, protocol->RKKT_encoded_data, 38);
        }

        bit_lib_copy_bits(protocol->data, 16, 1, protocol->RKKT_encoded_data, 45);

        bit_lib_copy_bits(protocol->data, 17, 8, protocol->RKKT_encoded_data, 47);
        bit_lib_copy_bits(protocol->data, 25, 7, protocol->RKKT_encoded_data, 56);

        bit_lib_copy_bits(protocol->data, 32, 8, protocol->RKKT_encoded_data, 65);

        bit_lib_copy_bits(protocol->data, 40, 8, protocol->RKKT_encoded_data, 74);
    }

}

void protocol_securakey_decoder_start(ProtocolSecurakey* protocol) {

    memset(protocol->RKKT_encoded_data, 0, SECURAKEY_RKKT_ENCODED_FULL_SIZE_BYTE);
    manchester_advance(
        protocol->decoder_manchester_state,
        ManchesterEventReset,
        &protocol->decoder_manchester_state,
        NULL);
}

bool protocol_securakey_decoder_feed(ProtocolSecurakey* protocol, bool level, uint32_t duration) {
    bool result = false;

    ManchesterEvent event = ManchesterEventReset;
    if(duration > SECURAKEY_READ_SHORT_TIME_LOW && duration < SECURAKEY_READ_SHORT_TIME_HIGH) {
        if(!level) {
            event = ManchesterEventShortHigh;
        } else {
            event = ManchesterEventShortLow;
        }
    } else if(duration > SECURAKEY_READ_LONG_TIME_LOW && duration < SECURAKEY_READ_LONG_TIME_HIGH) {
        if(!level) {
            event = ManchesterEventLongHigh;
        } else {
            event = ManchesterEventLongLow;
        }
    }

    if(event != ManchesterEventReset) {
        bool data;
        bool data_ok = manchester_advance(
            protocol->decoder_manchester_state, event, &protocol->decoder_manchester_state, &data);
        if(data_ok) {
            bit_lib_push_bit(
                protocol->RKKT_encoded_data, SECURAKEY_RKKT_ENCODED_FULL_SIZE_BYTE, data);
            if(protocol_securakey_can_be_decoded(protocol)) {
                protocol_securakey_decode(protocol);
                result = true;
            }
        }
    }
    return result;
}

void protocol_securakey_render_data(ProtocolSecurakey* protocol, FuriString* result) {
    if(bit_lib_get_bits_16(protocol->data, 0, 16) == 0) {
        protocol->bit_format = 0;
        furi_string_printf(
            result,
            "RKKTH Plaintext format\nCard number: %llu",
            bit_lib_get_bits_64(protocol->data, 0, 48));
    } else {
        if(bit_lib_get_bits(protocol->data, 0, 8) == 0) {
            protocol->bit_format = 26;
        } else {
            protocol->bit_format = 32;
        }
        furi_string_printf(
            result,
            "RKKT %u-bit format\nFacility code: %u\nCard number: %u",
            protocol->bit_format,
            bit_lib_get_bits_16(protocol->data, 0, 16),
            bit_lib_get_bits_16(protocol->data, 16, 16));
    }
}

bool protocol_securakey_encoder_start(ProtocolSecurakey* protocol) {

    memset(protocol->RKKTH_encoded_data, 0, SECURAKEY_RKKTH_ENCODED_FULL_SIZE_BYTE);
    memset(protocol->RKKT_encoded_data, 0, SECURAKEY_RKKT_ENCODED_FULL_SIZE_BYTE);
    if(bit_lib_get_bits_16(protocol->data, 0, 16) == 0) {

        bit_lib_set_bits(protocol->RKKTH_encoded_data, 0, 0b01111111, 8);
        bit_lib_set_bits(protocol->RKKTH_encoded_data, 8, 0b110, 3);

        bit_lib_copy_bits(protocol->RKKTH_encoded_data, 29, 8, protocol->data, 16);

        bit_lib_copy_bits(protocol->RKKTH_encoded_data, 38, 8, protocol->data, 24);
        bit_lib_copy_bits(protocol->RKKTH_encoded_data, 47, 8, protocol->data, 32);
        bit_lib_copy_bits(protocol->RKKTH_encoded_data, 56, 8, protocol->data, 40);
    } else {

        bit_lib_set_bits(protocol->RKKT_encoded_data, 0, 0b01111111, 8);
        bit_lib_set_bits(protocol->RKKT_encoded_data, 8, 0b11001, 5);
        if(bit_lib_get_bits(protocol->data, 0, 8) == 0) {
            protocol->bit_format = 26;

            bit_lib_set_bits(protocol->RKKT_encoded_data, 13, protocol->bit_format, 6);

            if(!bit_lib_test_parity(protocol->data, 8, 12, BitLibParityOdd, 12)) {
                bit_lib_set_bit(protocol->RKKT_encoded_data, 35, 1);
            }
            if(bit_lib_test_parity(protocol->data, 20, 12, BitLibParityOdd, 12)) {
                bit_lib_set_bit(protocol->RKKT_encoded_data, 63, 1);
            }

            bit_lib_copy_bits(protocol->RKKT_encoded_data, 36, 1, protocol->data, 8);

            bit_lib_copy_bits(protocol->RKKT_encoded_data, 38, 7, protocol->data, 9);
        } else {
            protocol->bit_format = 32;

            bit_lib_set_bits(protocol->RKKT_encoded_data, 13, protocol->bit_format, 6);

            if(!bit_lib_test_parity(protocol->data, 2, 15, BitLibParityOdd, 15)) {
                bit_lib_set_bit(protocol->RKKT_encoded_data, 29, 1);
            }
            if(bit_lib_test_parity(protocol->data, 17, 15, BitLibParityOdd, 15)) {
                bit_lib_set_bit(protocol->RKKT_encoded_data, 63, 1);
            }

            bit_lib_copy_bits(protocol->RKKT_encoded_data, 30, 7, protocol->data, 2);

            bit_lib_copy_bits(protocol->RKKT_encoded_data, 38, 7, protocol->data, 3);
        }

        bit_lib_copy_bits(protocol->RKKT_encoded_data, 45, 1, protocol->data, 16);

        bit_lib_copy_bits(protocol->RKKT_encoded_data, 47, 8, protocol->data, 17);
        bit_lib_copy_bits(protocol->RKKT_encoded_data, 56, 7, protocol->data, 25);

        bit_lib_copy_bits(protocol->RKKT_encoded_data, 65, 8, protocol->data, 32);

        bit_lib_copy_bits(protocol->RKKT_encoded_data, 74, 8, protocol->data, 40);
    }

    protocol->encoded_data_index = 0;
    protocol->encoded_polarity = true;
    return true;
}

LevelDuration protocol_securakey_encoder_yield(ProtocolSecurakey* protocol) {
    if(bit_lib_get_bits_16(protocol->data, 0, 16) == 0) {
        bool level = bit_lib_get_bit(protocol->RKKTH_encoded_data, protocol->encoded_data_index);
        uint32_t duration = SECURAKEY_CLOCK_PER_BIT / 2;
        if(protocol->encoded_polarity) {
            protocol->encoded_polarity = false;
        } else {
            level = !level;
            protocol->encoded_polarity = true;
            bit_lib_increment_index(
                protocol->encoded_data_index, SECURAKEY_RKKTH_ENCODED_FULL_SIZE_BITS);
        }
        return level_duration_make(level, duration);
    } else {
        bool level = bit_lib_get_bit(protocol->RKKT_encoded_data, protocol->encoded_data_index);
        uint32_t duration = SECURAKEY_CLOCK_PER_BIT / 2;
        if(protocol->encoded_polarity) {
            protocol->encoded_polarity = false;
        } else {
            level = !level;
            protocol->encoded_polarity = true;
            bit_lib_increment_index(
                protocol->encoded_data_index, SECURAKEY_RKKT_ENCODED_FULL_SIZE_BITS);
        }
        return level_duration_make(level, duration);
    }
}

bool protocol_securakey_write_data(ProtocolSecurakey* protocol, void* data) {
    protocol_securakey_encoder_start(protocol);
    LFRFIDWriteRequest* request = (LFRFIDWriteRequest*)data;
    bool result = false;

    if(bit_lib_get_bits_16(protocol->data, 0, 16) == 0) {
        if(request->write_type == LFRFIDWriteTypeT5577) {
            request->t5577.block[0] =
                (LFRFID_T5577_MODULATION_MANCHESTER | LFRFID_T5577_BITRATE_RF_40 |
                 (2
                  << LFRFID_T5577_MAXBLOCK_SHIFT));
            request->t5577.block[1] = bit_lib_get_bits_32(protocol->RKKTH_encoded_data, 0, 32);
            request->t5577.block[2] = bit_lib_get_bits_32(protocol->RKKTH_encoded_data, 32, 32);
            request->t5577.blocks_to_write = 3;
            result = true;
        } else if(request->write_type == LFRFIDWriteTypeEM4305) {
            request->em4305.word[4] =
                (EM4x05_MODULATION_MANCHESTER | EM4x05_SET_BITRATE(40) |
                 (6 << EM4x05_MAXBLOCK_SHIFT));
            uint32_t encoded_data_reversed[2] = {0};
            for(uint8_t i = 0; i < 64; i++) {
                encoded_data_reversed[i / 32] =
                    (encoded_data_reversed[i / 32] << 1) |
                    (bit_lib_get_bit(protocol->RKKTH_encoded_data, (63 - i)) & 1);
            }
            request->em4305.word[5] = encoded_data_reversed[1];
            request->em4305.word[6] = encoded_data_reversed[0];
            request->em4305.mask = 0x70;
            result = true;
        }
    } else {
        if(request->write_type == LFRFIDWriteTypeT5577) {
            request->t5577.block[0] =
                (LFRFID_T5577_MODULATION_MANCHESTER | LFRFID_T5577_BITRATE_RF_40 |
                 (3
                  << LFRFID_T5577_MAXBLOCK_SHIFT));
            request->t5577.block[1] = bit_lib_get_bits_32(protocol->RKKT_encoded_data, 0, 32);
            request->t5577.block[2] = bit_lib_get_bits_32(protocol->RKKT_encoded_data, 32, 32);
            request->t5577.block[3] = bit_lib_get_bits_32(protocol->RKKT_encoded_data, 64, 32);
            request->t5577.blocks_to_write = 4;
            result = true;
        } else if(request->write_type == LFRFIDWriteTypeEM4305) {
            request->em4305.word[4] =
                (EM4x05_MODULATION_MANCHESTER | EM4x05_SET_BITRATE(40) |
                 (7 << EM4x05_MAXBLOCK_SHIFT));
            uint32_t encoded_data_reversed[3] = {0};
            for(uint8_t i = 0; i < 96; i++) {
                encoded_data_reversed[i / 32] =
                    (encoded_data_reversed[i / 32] << 1) |
                    (bit_lib_get_bit(protocol->RKKT_encoded_data, (95 - i)) & 1);
            }
            request->em4305.word[5] = encoded_data_reversed[2];
            request->em4305.word[6] = encoded_data_reversed[1];
            request->em4305.word[7] = encoded_data_reversed[0];
            request->em4305.mask = 0xF0;
            result = true;
        }
    }
    return result;
}

const ProtocolBase protocol_securakey = {
    .name = "Radio Key",
    .manufacturer = "Securakey",
    .data_size = SECURAKEY_DECODED_DATA_SIZE_BYTES,
    .features = LFRFIDFeatureASK,
    .validate_count = 3,
    .alloc = (ProtocolAlloc)protocol_securakey_alloc,
    .free = (ProtocolFree)protocol_securakey_free,
    .get_data = (ProtocolGetData)protocol_securakey_get_data,
    .decoder =
        {
            .start = (ProtocolDecoderStart)protocol_securakey_decoder_start,
            .feed = (ProtocolDecoderFeed)protocol_securakey_decoder_feed,
        },
    .encoder =
        {
            .start = (ProtocolEncoderStart)protocol_securakey_encoder_start,
            .yield = (ProtocolEncoderYield)protocol_securakey_encoder_yield,
        },
    .render_data = (ProtocolRenderData)protocol_securakey_render_data,
    .render_brief_data = (ProtocolRenderData)protocol_securakey_render_data,
    .write_data = (ProtocolWriteData)protocol_securakey_write_data,
};
