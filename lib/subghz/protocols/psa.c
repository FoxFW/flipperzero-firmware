#include "psa.h"

#include "common.h"
#include "../encode_plugin.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"
#include <lib/toolbox/manchester_decoder.h>

#define TAG "SubGhzProtocolPSA"

static const SubGhzBlockConst subghz_protocol_psa_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 100,
    .min_count_bit_for_found = 128,
};

#define PSA_TE_SHORT_125 0x7d
#define PSA_TE_LONG_250 0xfa
#define PSA_TE_END_1000 1000
#define PSA_TE_END_500 500
#define PSA_TOLERANCE_99 99
#define PSA_TOLERANCE_100 100
#define PSA_TOLERANCE_49 0x31
#define PSA_TOLERANCE_50 0x32
#define PSA_PATTERN_THRESHOLD_1 0x46
#define PSA_PATTERN_THRESHOLD_2 0x45
#define PSA_MAX_BITS 0x79
#define PSA_KEY1_BITS 0x40
#define PSA_KEY2_BITS 0x50

#define PSA_AM_PRE_MIN 60
#define PSA_AM_PRE_MAX 200
#define PSA_AM_PRE_THRESHOLD 8
#define PSA_AM_PRE_GLITCH_DECAY 2
#define PSA_AM_PRE_MAX_GLITCH 3
#define PSA_AM_RESYNC_BITS 8
#define PSA_AM_LONG_MIN 360U
#define PSA_AM_LONG_MAX 560U
#define PSA_AM_LONG_MIN_PCT 4U

#define TEA_DELTA 0x9E3779B9U
#define TEA_ROUNDS 32

#define PSA_BF1_CONST_U4 0x0E0F5C41U
#define PSA_BF1_CONST_U5 0x0F5C4123U

static const uint32_t PSA_BF1_KEY_SCHEDULE[4] = {
    0x4A434915U,
    0xD6743C2BU,
    0x1F29D308U,
    0xE6B79A64U,
};

static const uint32_t PSA_BF2_KEY_SCHEDULE[4] = {
    0x4039C240U,
    0xEDA92CABU,
    0x4306C02AU,
    0x02192A04U,
};

#define PSA_BF1_START 0x23000000U
#define PSA_BF1_END 0x24000000U
#define PSA_BF2_START 0xF3000000U
#define PSA_BF2_END 0xF4000000U

typedef enum {
    PSADecoderState0 = 0,
    PSADecoderState1 = 1,
    PSADecoderState2 = 2,
    PSADecoderState3 = 3,
    PSADecoderState4 = 4,
} PSADecoderState;

struct SubGhzProtocolDecoderPSA {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t state;
    uint32_t prev_duration;

    uint32_t decode_data_low;
    uint32_t decode_data_high;
    uint8_t decode_count_bit;

    uint32_t key1_low;
    uint32_t key1_high;
    uint16_t validation_field;
    uint32_t key2_low;
    uint32_t key2_high;

    uint32_t status_flag;
    uint16_t decrypted;
    uint8_t mode_serialize;

    uint8_t decrypted_button;
    uint32_t decrypted_serial;
    uint32_t decrypted_counter;
    uint16_t decrypted_crc;
    uint32_t decrypted_seed;
    uint8_t decrypted_type;

    uint16_t pattern_counter;
    ManchesterState manchester_state;

    uint32_t last_key1_low;
    uint32_t last_key1_high;

    uint32_t te_sum;
    uint16_t te_count;
    uint32_t te_detected;

    uint8_t pre_glitch;
    uint8_t am_await_high;
    uint8_t am_bits[96];
    uint8_t am_bits_len;
    uint16_t am_long_count;
    uint16_t am_pulse_count;
};

struct SubGhzProtocolEncoderPSA {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint32_t key1_low;
    uint32_t key1_high;
    uint16_t validation_field;
    uint32_t key2_low;
    uint32_t counter;
    uint8_t button;
    uint8_t type;
    uint8_t seed;
    uint8_t mode;
    uint32_t serial;
    uint16_t crc;
    bool is_running;
    bool raw_replay;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderPSA);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderPSA);

const SubGhzProtocolDecoder subghz_protocol_psa_decoder = {
    .alloc = subghz_protocol_decoder_psa_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_psa_feed,
    .reset = subghz_protocol_decoder_psa_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_psa_serialize,
    .deserialize = subghz_protocol_decoder_psa_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_psa_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_psa");

const SubGhzProtocol subghz_protocol_psa = {
    .name = SUBGHZ_PROTOCOL_PSA_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_FM |
            SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = &subghz_protocol_psa_decoder,
    .encoder = &subghz_protocol_psa_encoder_ext.vtable,
};

static uint32_t psa_abs_diff(uint32_t a, uint32_t b) {
    if(a < b) {
        return b - a;
    } else {
        return a - b;
    }
}

static void psa_setup_byte_buffer(uint8_t* buffer, uint32_t key1_low, uint32_t key1_high, uint32_t key2_low) {
    for(int i = 0; i < 8; i++) {
        int shift = i * 8;
        uint8_t byte_val;
        if(shift < 32) {
            byte_val = (uint8_t)((key1_low >> shift) & 0xFF);
        } else {
            byte_val = (uint8_t)((key1_high >> (shift - 32)) & 0xFF);
        }
        buffer[7 - i] = byte_val;
    }
    buffer[9] = (uint8_t)(key2_low & 0xFF);
    buffer[8] = (uint8_t)((key2_low >> 8) & 0xFF);
}

static void psa_calculate_checksum(uint8_t* buffer) {
    uint32_t checksum = 0;
    for(int i = 2; i < 8; i++) {
        checksum += (buffer[i] & 0xF) + ((buffer[i] >> 4) & 0xF);
    }
    buffer[11] = (uint8_t)((checksum * 0x10) & 0xFF);
}

static void psa_copy_reverse(uint8_t* temp, uint8_t* source) {
    temp[0] = source[5];
    temp[1] = source[4];
    temp[2] = source[3];
    temp[3] = source[2];
    temp[4] = source[9];
    temp[5] = source[8];
    temp[6] = source[7];
    temp[7] = source[6];
}

static void psa_second_stage_xor_decrypt(uint8_t* buffer) {
    uint8_t temp[8];
    psa_copy_reverse(temp, buffer);
    buffer[2] = temp[0] ^ temp[6];
    buffer[3] = temp[2] ^ temp[0];
    buffer[4] = temp[6] ^ temp[3];
    buffer[5] = temp[7] ^ temp[1];
    buffer[6] = temp[3] ^ temp[1];
    buffer[7] = temp[6] ^ temp[4] ^ temp[5];
}

__attribute__((optimize("O3"), always_inline)) static inline void
    psa_tea_encrypt(
        uint32_t* restrict v0,
        uint32_t* restrict v1,
        const uint32_t* restrict key) {
    uint32_t a = *v0, b = *v1;
    uint32_t sum = 0;
    for(int i = 0; i < TEA_ROUNDS; i++) {
        uint32_t temp = key[sum & 3] + sum;
        sum += TEA_DELTA;
        a += (temp ^ (((b >> 5) ^ (b << 4)) + b));
        temp = key[(sum >> 11) & 3] + sum;
        b += (temp ^ (((a >> 5) ^ (a << 4)) + a));
    }
    *v0 = a;
    *v1 = b;
}

__attribute__((optimize("O3"), always_inline)) static inline void
    psa_tea_decrypt(
        uint32_t* restrict v0,
        uint32_t* restrict v1,
        const uint32_t* restrict key) {
    uint32_t a = *v0, b = *v1;
    uint32_t sum = TEA_DELTA * TEA_ROUNDS;
    for(int i = 0; i < TEA_ROUNDS; i++) {
        uint32_t temp = key[(sum >> 11) & 3] + sum;
        sum -= TEA_DELTA;
        b -= (temp ^ (((a >> 5) ^ (a << 4)) + a));
        temp = key[sum & 3] + sum;
        a -= (temp ^ (((b >> 5) ^ (b << 4)) + b));
    }
    *v0 = a;
    *v1 = b;
}

typedef struct {
    uint32_t s0[TEA_ROUNDS];
    uint32_t s1[TEA_ROUNDS];
} PsaTeaSchedule;

__attribute__((optimize("O3"), always_inline)) static inline void
    psa_tea_build_schedule(const uint32_t* key, PsaTeaSchedule* out) {
    for(int i = 0; i < TEA_ROUNDS; i++) {
        uint32_t sum0 = (uint32_t)((uint64_t)i * TEA_DELTA);
        uint32_t sum1 = (uint32_t)((uint64_t)(i + 1) * TEA_DELTA);
        out->s0[i] = key[sum0 & 3] + sum0;
        out->s1[i] = key[(sum1 >> 11) & 3] + sum1;
    }
}

__attribute__((optimize("O3"), always_inline)) static inline void
    psa_tea_encrypt_with_schedule(uint32_t* restrict v0, uint32_t* restrict v1, const PsaTeaSchedule* sched) {
    uint32_t a = *v0, b = *v1;
    for(int i = 0; i < TEA_ROUNDS; i++) {
        a += (sched->s0[i] ^ (((b >> 5) ^ (b << 4)) + b));
        b += (sched->s1[i] ^ (((a >> 5) ^ (a << 4)) + a));
    }
    *v0 = a;
    *v1 = b;
}

static void psa_prepare_tea_data(uint8_t* buffer, uint32_t* w0, uint32_t* w1) {
    *w0 = ((uint32_t)buffer[3] << 16) | ((uint32_t)buffer[2] << 24) |
          ((uint32_t)buffer[4] << 8) | (uint32_t)buffer[5];
    *w1 = ((uint32_t)buffer[7] << 16) | ((uint32_t)buffer[6] << 24) |
          ((uint32_t)buffer[8] << 8) | (uint32_t)buffer[9];
}

static uint8_t psa_calculate_tea_crc(uint32_t v0, uint32_t v1) {
    uint32_t crc = ((v0 >> 24) & 0xFF) + ((v0 >> 16) & 0xFF) +
                   ((v0 >> 8) & 0xFF) + (v0 & 0xFF);
    crc += ((v1 >> 24) & 0xFF) + ((v1 >> 16) & 0xFF) + ((v1 >> 8) & 0xFF);
    return (uint8_t)(crc & 0xFF);
}

static const uint16_t psa_crc16_table[256] = {
    0x0000, 0x8005, 0x800F, 0x000A, 0x801B, 0x001E, 0x0014, 0x8011,
    0x8033, 0x0036, 0x003C, 0x8039, 0x0028, 0x802D, 0x8027, 0x0022,
    0x8063, 0x0066, 0x006C, 0x8069, 0x0078, 0x807D, 0x8077, 0x0072,
    0x0050, 0x8055, 0x805F, 0x005A, 0x804B, 0x004E, 0x0044, 0x8041,
    0x80C3, 0x00C6, 0x00CC, 0x80C9, 0x00D8, 0x80DD, 0x80D7, 0x00D2,
    0x00F0, 0x80F5, 0x80FF, 0x00FA, 0x80EB, 0x00EE, 0x00E4, 0x80E1,
    0x00A0, 0x80A5, 0x80AF, 0x00AA, 0x80BB, 0x00BE, 0x00B4, 0x80B1,
    0x8093, 0x0096, 0x009C, 0x8099, 0x0088, 0x808D, 0x8087, 0x0082,
    0x8183, 0x0186, 0x018C, 0x8189, 0x0198, 0x819D, 0x8197, 0x0192,
    0x01B0, 0x81B5, 0x81BF, 0x01BA, 0x81AB, 0x01AE, 0x01A4, 0x81A1,
    0x01E0, 0x81E5, 0x81EF, 0x01EA, 0x81FB, 0x01FE, 0x01F4, 0x81F1,
    0x81D3, 0x01D6, 0x01DC, 0x81D9, 0x01C8, 0x81CD, 0x81C7, 0x01C2,
    0x0140, 0x8145, 0x814F, 0x014A, 0x815B, 0x015E, 0x0154, 0x8151,
    0x8173, 0x0176, 0x017C, 0x8179, 0x0168, 0x816D, 0x8167, 0x0162,
    0x8123, 0x0126, 0x012C, 0x8129, 0x0138, 0x813D, 0x8137, 0x0132,
    0x0110, 0x8115, 0x811F, 0x011A, 0x810B, 0x010E, 0x0104, 0x8101,
    0x8303, 0x0306, 0x030C, 0x8309, 0x0318, 0x831D, 0x8317, 0x0312,
    0x0330, 0x8335, 0x833F, 0x033A, 0x832B, 0x032E, 0x0324, 0x8321,
    0x0360, 0x8365, 0x836F, 0x036A, 0x837B, 0x037E, 0x0374, 0x8371,
    0x8353, 0x0356, 0x035C, 0x8359, 0x0348, 0x834D, 0x8347, 0x0342,
    0x03C0, 0x83C5, 0x83CF, 0x03CA, 0x83DB, 0x03DE, 0x03D4, 0x83D1,
    0x83F3, 0x03F6, 0x03FC, 0x83F9, 0x03E8, 0x83ED, 0x83E7, 0x03E2,
    0x83A3, 0x03A6, 0x03AC, 0x83A9, 0x03B8, 0x83BD, 0x83B7, 0x03B2,
    0x0390, 0x8395, 0x839F, 0x039A, 0x838B, 0x038E, 0x0384, 0x8381,
    0x0280, 0x8285, 0x828F, 0x028A, 0x829B, 0x029E, 0x0294, 0x8291,
    0x82B3, 0x02B6, 0x02BC, 0x82B9, 0x02A8, 0x82AD, 0x82A7, 0x02A2,
    0x82E3, 0x02E6, 0x02EC, 0x82E9, 0x02F8, 0x82FD, 0x82F7, 0x02F2,
    0x02D0, 0x82D5, 0x82DF, 0x02DA, 0x82CB, 0x02CE, 0x02C4, 0x82C1,
    0x8243, 0x0246, 0x024C, 0x8249, 0x0258, 0x825D, 0x8257, 0x0252,
    0x0270, 0x8275, 0x827F, 0x027A, 0x826B, 0x026E, 0x0264, 0x8261,
    0x0220, 0x8225, 0x822F, 0x022A, 0x823B, 0x023E, 0x0234, 0x8231,
    0x8213, 0x0216, 0x021C, 0x8219, 0x0208, 0x820D, 0x8207, 0x0202,
};

static uint16_t psa_calculate_crc16_bf2(uint8_t* buffer, int length) {
    uint16_t crc = 0;
    for(int i = 0; i < length; i++) {
        crc = (crc << 8) ^ psa_crc16_table[((crc >> 8) ^ buffer[i]) & 0xFF];
    }
    return crc;
}

static void psa_unpack_tea_result_to_buffer(uint8_t* buffer, uint32_t v0, uint32_t v1) {
    buffer[2] = (uint8_t)((v0 >> 24) & 0xFF);
    buffer[3] = (uint8_t)((v0 >> 16) & 0xFF);
    buffer[4] = (uint8_t)((v0 >> 8) & 0xFF);
    buffer[5] = (uint8_t)(v0 & 0xFF);
    buffer[6] = (uint8_t)((v1 >> 24) & 0xFF);
    buffer[7] = (uint8_t)((v1 >> 16) & 0xFF);
    buffer[8] = (uint8_t)((v1 >> 8) & 0xFF);
    buffer[9] = (uint8_t)(v1 & 0xFF);
}

static void psa_extract_fields_mode23(uint8_t* buffer, SubGhzProtocolDecoderPSA* instance) {
    instance->decrypted_button = buffer[8] & 0xF;
    instance->decrypted_serial = ((uint32_t)buffer[3] << 8) | ((uint32_t)buffer[2] << 16) | (uint32_t)buffer[4];
    instance->decrypted_counter = (uint32_t)buffer[6] | ((uint32_t)buffer[5] << 8);
    instance->decrypted_crc = (uint16_t)buffer[7];
    instance->decrypted_type = 0x23;
    instance->decrypted_seed = instance->decrypted_serial;
}

static void psa_extract_fields_mode36(uint8_t* buffer, SubGhzProtocolDecoderPSA* instance) {
    instance->decrypted_button = (buffer[5] >> 4) & 0xF;
    instance->decrypted_serial = ((uint32_t)buffer[3] << 8) | ((uint32_t)buffer[2] << 16) | (uint32_t)buffer[4];
    instance->decrypted_counter = ((uint32_t)buffer[7] << 8) | ((uint32_t)buffer[6] << 16) |
                                   (uint32_t)buffer[8] | (((uint32_t)buffer[5] & 0xF) << 24);
    instance->decrypted_crc = (uint16_t)buffer[9];
    instance->decrypted_type = 0x36;
    instance->decrypted_seed = instance->decrypted_serial;
}

__attribute__((optimize("O3"))) static bool psa_brute_force_decrypt_bf1(SubGhzProtocolDecoderPSA* instance, uint8_t* buffer, uint32_t w0, uint32_t w1, PsaDecryptProgressCallback progress_cb, void* progress_ctx) {
	uint32_t bf1_total = PSA_BF1_END - PSA_BF1_START;
	PsaTeaSchedule bf1_sched;
	psa_tea_build_schedule(PSA_BF1_KEY_SCHEDULE, &bf1_sched);
	for(uint32_t counter = PSA_BF1_START; counter < PSA_BF1_END; counter++) {
		if(progress_cb && ((counter - PSA_BF1_START) & 0xFFFF) == 0) {
			uint8_t pct = (uint8_t)(((uint64_t)(counter - PSA_BF1_START) * 50) / bf1_total);
			if(!progress_cb(pct, counter - PSA_BF1_START, progress_ctx)) return false;
		}
        uint32_t wk2 = PSA_BF1_CONST_U4;
        uint32_t wk3 = counter;
        psa_tea_encrypt_with_schedule(&wk2, &wk3, &bf1_sched);

        uint32_t wk0 = (counter << 8) | 0x0E;
        uint32_t wk1 = PSA_BF1_CONST_U5;
        psa_tea_encrypt_with_schedule(&wk0, &wk1, &bf1_sched);

        uint32_t working_key[4] = {wk0, wk1, wk2, wk3};

        uint32_t dec_v0 = w0;
        uint32_t dec_v1 = w1;
        psa_tea_decrypt(&dec_v0, &dec_v1, working_key);

        if((counter & 0xFFFFFF) == (dec_v0 >> 8)) {
            uint8_t crc = psa_calculate_tea_crc(dec_v0, dec_v1);
            if(crc == (dec_v1 & 0xFF)) {
                psa_unpack_tea_result_to_buffer(buffer, dec_v0, dec_v1);
                psa_extract_fields_mode36(buffer, instance);
				instance->decrypted_seed = counter;
                return true;
            }
        }
    }
    return false;
}

__attribute__((optimize("O3"))) static bool psa_brute_force_decrypt_bf2(SubGhzProtocolDecoderPSA* instance, uint8_t* buffer, uint32_t w0, uint32_t w1, PsaDecryptProgressCallback progress_cb, void* progress_ctx) {
	uint32_t bf2_total = PSA_BF2_END - PSA_BF2_START;
	for(uint32_t counter = PSA_BF2_START; counter < PSA_BF2_END; counter++) {
		if(progress_cb && ((counter - PSA_BF2_START) & 0xFFFF) == 0) {
			uint8_t pct = 50 + (uint8_t)(((uint64_t)(counter - PSA_BF2_START) * 50) / bf2_total);
			if(!progress_cb(pct, 0x1000000 + (counter - PSA_BF2_START), progress_ctx)) return false;
		}
        uint32_t working_key[4] = {
            PSA_BF2_KEY_SCHEDULE[0] ^ counter,
            PSA_BF2_KEY_SCHEDULE[1] ^ counter,
            PSA_BF2_KEY_SCHEDULE[2] ^ counter,
            PSA_BF2_KEY_SCHEDULE[3] ^ counter,
        };

        uint32_t dec_v0 = w0;
        uint32_t dec_v1 = w1;
        psa_tea_decrypt(&dec_v0, &dec_v1, working_key);

        if((counter & 0xFFFFFF) == (dec_v0 >> 8)) {
            psa_unpack_tea_result_to_buffer(buffer, dec_v0, dec_v1);

            // CRC-16/BUYPASS input is natural big-endian; the two middle dec_v0
            // bytes must NOT be swapped. (Fix 2026-10-03: they were, so a genuine
            // type-0x36 BF2 frame never validated. Confirmed against the
            // Ghidra-reconstructed authoritative psa2.c and a host round-trip.)
            uint8_t crc_buffer[6] = {
                (uint8_t)((dec_v0 >> 24) & 0xFF),
                (uint8_t)((dec_v0 >> 16) & 0xFF),
                (uint8_t)((dec_v0 >> 8) & 0xFF),
                (uint8_t)(dec_v0 & 0xFF),
                (uint8_t)((dec_v1 >> 24) & 0xFF),
                (uint8_t)((dec_v1 >> 16) & 0xFF),
            };
            uint16_t crc16 = psa_calculate_crc16_bf2(crc_buffer, 6);
            uint16_t expected_crc = (((dec_v1 >> 16) & 0xFF) << 8) | (dec_v1 & 0xFF);

            if(crc16 == expected_crc) {
                psa_extract_fields_mode36(buffer, instance);
				instance->decrypted_seed = counter;
                return true;
            }
        }
    }
    return false;
}

static bool psa_direct_xor_decrypt(SubGhzProtocolDecoderPSA* instance, uint8_t* buffer) {
    psa_calculate_checksum(buffer);
    uint8_t checksum = buffer[11];
    uint8_t key2_high = buffer[8];

    uint8_t validation_result = (checksum ^ key2_high) & 0xF0;
    if(validation_result == 0) {
        buffer[13] = buffer[9] ^ buffer[8];
        psa_second_stage_xor_decrypt(buffer);
        psa_extract_fields_mode23(buffer, instance);
        return true;
    }
    return false;
}

static void psa_decrypt_fast(SubGhzProtocolDecoderPSA* instance) {
    uint8_t buffer[48] = {0};
    psa_setup_byte_buffer(buffer, instance->key1_low, instance->key1_high, instance->key2_low);
    if(psa_direct_xor_decrypt(instance, buffer)) {
        instance->mode_serialize = 0x23;
        instance->decrypted = 0x50;
    } else {
        instance->decrypted = 0x00;
        instance->mode_serialize = 0x36;
    }
}

static void psa_decrypt_full(SubGhzProtocolDecoderPSA* instance, PsaDecryptProgressCallback progress_cb, void* progress_ctx) {
    uint8_t buffer[48] = {0};
    psa_setup_byte_buffer(buffer, instance->key1_low, instance->key1_high, instance->key2_low);

    uint8_t mode = instance->mode_serialize;

    if(mode == 1 || mode == 2) {
        if(psa_direct_xor_decrypt(instance, buffer)) {
            mode = 0x23;
        } else {
            mode = 0x36;
        }
    }

    if(mode == 0x23) {
        if(psa_direct_xor_decrypt(instance, buffer)) {
            instance->mode_serialize = 0x23;
            instance->decrypted = 0x50;
            return;
        }
    } else if(mode == 0x36) {
        uint32_t w0, w1;
        psa_prepare_tea_data(buffer, &w0, &w1);
        if(psa_brute_force_decrypt_bf1(instance, buffer, w0, w1, progress_cb, progress_ctx)) {
            instance->mode_serialize = 0x36;
            instance->decrypted = 0x50;
            return;
        }
        if(psa_brute_force_decrypt_bf2(instance, buffer, w0, w1, progress_cb, progress_ctx)) {
            instance->mode_serialize = 0x36;
            instance->decrypted = 0x50;
            return;
        }
    } else {
        if(psa_direct_xor_decrypt(instance, buffer)) {
            instance->mode_serialize = 0x23;
            instance->decrypted = 0x50;
            return;
        }
        uint32_t w0, w1;
        psa_prepare_tea_data(buffer, &w0, &w1);
        if(psa_brute_force_decrypt_bf1(instance, buffer, w0, w1, progress_cb, progress_ctx)) {
            instance->mode_serialize = 0x36;
            instance->decrypted = 0x50;
            return;
        }
        if(psa_brute_force_decrypt_bf2(instance, buffer, w0, w1, progress_cb, progress_ctx)) {
            instance->mode_serialize = 0x36;
            instance->decrypted = 0x50;
            return;
        }
    }
    instance->decrypted = 0x00;
}

void* subghz_protocol_decoder_psa_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderPSA* instance = malloc(sizeof(SubGhzProtocolDecoderPSA));
    if(instance) {
        memset(instance, 0, sizeof(SubGhzProtocolDecoderPSA));
        instance->base.protocol = &subghz_protocol_psa;
        instance->generic.protocol_name = instance->base.protocol->name;
        instance->manchester_state = ManchesterStateMid1;
    }
    return instance;
}

void subghz_protocol_decoder_psa_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderPSA* instance = context;

    instance->state = 0;
    instance->status_flag = 0;
    instance->mode_serialize = 0;
    instance->key1_low = 0;
    instance->key1_high = 0;
    instance->key2_low = 0;
    instance->key2_high = 0;
    instance->validation_field = 0;
    instance->decode_data_low = 0;
    instance->decode_data_high = 0;
    instance->decode_count_bit = 0;
    instance->pattern_counter = 0;
    instance->manchester_state = ManchesterStateMid1;
    instance->prev_duration = 0;
    instance->decrypted = 0;

    instance->decrypted_button = 0;
    instance->decrypted_serial = 0;
    instance->decrypted_counter = 0;
    instance->decrypted_crc = 0;
    instance->decrypted_seed = 0;
    instance->decrypted_type = 0;

    instance->pre_glitch = 0;
    instance->am_await_high = 0;
    instance->am_bits_len = 0;
    instance->am_long_count = 0;
    instance->am_pulse_count = 0;
    instance->te_sum = 0;
    instance->te_count = 0;
    instance->te_detected = 0;
}

/* Emit gate (v2.0.14, 2026-10-13): only fire when the signal's long-pulse
 * ratio is PSA-typical. am_long_count/am_pulse_count is the same metric the
 * "PSA-typ" vs "fast-OOK" label in get_string uses (>= PSA_AM_LONG_MIN_PCT).
 * A genuine PSA capture is long-pulse-rich; a foreign bi-phase OOK stream
 * (e.g. a Fiat Hitag2 remote) is long-pulse-poor and was being chopped into
 * ever-changing 128-bit "Unknown Signal" frames that showed up as a bogus
 * PSA result. Validated across the SD corpus: all 26 genuine PSA RAW captures
 * keep firing (recognized or PSA-typ), every fast-OOK-only latch (Fiat, GM,
 * Ford, Opel, ...) is suppressed. am_pulse_count==0 (FM path) stays PSA-like,
 * so FM PSA is unaffected. */
#define PSA_FIRE_CALLBACK_IF_NEW(instance)                                          \
    do {                                                                             \
        bool _is_dup = ((instance)->key1_low  == (instance)->last_key1_low &&       \
                        (instance)->key1_high == (instance)->last_key1_high);       \
        bool _psa_like = ((instance)->am_pulse_count == 0U) ||                       \
                         ((uint32_t)(instance)->am_long_count * 100U >=              \
                          (uint32_t)(instance)->am_pulse_count * PSA_AM_LONG_MIN_PCT); \
        if(!_is_dup && _psa_like) {                                                  \
            (instance)->last_key1_low  = (instance)->key1_low;                      \
            (instance)->last_key1_high = (instance)->key1_high;                     \
            if((instance)->base.callback) {                                          \
                (instance)->base.callback(&(instance)->base, (instance)->base.context); \
            }                                                                        \
        }                                                                            \
    } while(0)

static void psa_add_am_bit(SubGhzProtocolDecoderPSA* instance, uint8_t bit) {
    bit = bit ? 0 : 1;
    if(instance->am_bits_len >= 96) {
        for(uint8_t i = 1; i < 96; i++) {
            instance->am_bits[i - 1] = instance->am_bits[i];
        }
        instance->am_bits[95] = bit;
    } else {
        instance->am_bits[instance->am_bits_len++] = bit;
    }
    uint32_t carry = (instance->decode_data_low >> 31) & 1;
    instance->decode_data_low = (instance->decode_data_low << 1) | bit;
    instance->decode_data_high = (instance->decode_data_high << 1) | carry;
    instance->decode_count_bit++;
    if(instance->decode_count_bit == PSA_KEY1_BITS) {
        instance->key1_low = instance->decode_data_low;
        instance->key1_high = instance->decode_data_high;
        instance->decode_data_low = 0;
        instance->decode_data_high = 0;
    }
}

static bool psa_am_frame_at(
    const SubGhzProtocolDecoderPSA* instance,
    uint8_t off,
    uint32_t* hi,
    uint32_t* lo,
    uint32_t* k2,
    uint8_t* nib) {
    if(instance->am_bits_len < off + 80) return false;
    uint32_t h = 0, l = 0, k = 0;
    uint8_t i;
    for(i = 0; i < 32; i++) h = (h << 1) | instance->am_bits[off + i];
    for(i = 32; i < 64; i++) l = (l << 1) | instance->am_bits[off + i];
    for(i = 64; i < 80; i++) k = (k << 1) | instance->am_bits[off + i];
    *hi = h;
    *lo = l;
    *k2 = k;
    *nib = (uint8_t)((h >> 16) & 0xF);
    return true;
}

static bool psa_am_varied(uint32_t hi, uint32_t lo) {
    uint8_t vals[8] = {
        (uint8_t)((hi >> 24) & 0xFF),
        (uint8_t)((hi >> 16) & 0xFF),
        (uint8_t)((hi >> 8) & 0xFF),
        (uint8_t)(hi & 0xFF),
        (uint8_t)((lo >> 24) & 0xFF),
        (uint8_t)((lo >> 16) & 0xFF),
        (uint8_t)((lo >> 8) & 0xFF),
        (uint8_t)(lo & 0xFF)};
    uint8_t n = 0;
    for(uint8_t i = 0; i < 8; i++) {
        bool seen = false;
        for(uint8_t j = 0; j < i; j++) {
            if(vals[j] == vals[i]) {
                seen = true;
                break;
            }
        }
        if(!seen) n++;
    }
    return n >= 4;
}

static bool psa_am_complete(SubGhzProtocolDecoderPSA* instance) {
    if(instance->am_bits_len < 80) return false;

    uint8_t max_off = instance->am_bits_len - 80;
    if(max_off > 8) max_off = 8;

    int16_t best_off = -1;
    int16_t best_score = -1;
    uint32_t hi = 0, lo = 0, k2 = 0;
    uint8_t nib = 0;

    for(uint8_t off = 0; off <= max_off; off++) {
        if(!psa_am_frame_at(instance, off, &hi, &lo, &k2, &nib)) continue;
        if(nib != 0xA) continue;
        if(!psa_am_varied(hi, lo)) continue;
        int16_t score = 0;
        int8_t bit = 31;
        while(bit >= 20 && ((hi >> bit) & 1)) {
            score++;
            bit--;
        }
        if(score > best_score) {
            best_score = score;
            best_off = (int16_t)off;
        }
    }

    if(best_off < 0) {
        instance->decode_data_low = 0;
        instance->decode_data_high = 0;
        instance->decode_count_bit = 0;
        instance->am_bits_len = 0;
        instance->state = PSADecoderState0;
        return false;
    }

    psa_am_frame_at(instance, (uint8_t)best_off, &hi, &lo, &k2, &nib);
    instance->key1_high = hi;
    instance->key1_low = lo;
    instance->key2_low = k2;
    instance->key2_high = 0;
    instance->validation_field = (uint16_t)(k2 & 0xFFFF);
    instance->mode_serialize = 2;
    instance->status_flag = 0x80;

    uint8_t buffer[48] = {0};
    psa_setup_byte_buffer(buffer, instance->key1_low, instance->key1_high, instance->key2_low);
    if(psa_direct_xor_decrypt(instance, buffer)) {
        instance->mode_serialize = 0x23;
        instance->decrypted = 0x50;
    } else {
        instance->decrypted = 0x00;
        instance->mode_serialize = 0x36;
    }

    instance->generic.data = ((uint64_t)instance->key1_high << 32) | instance->key1_low;
    instance->generic.data_count_bit = 64;
    instance->decoder.decode_data = instance->generic.data;
    instance->decoder.decode_count_bit = 64;

    PSA_FIRE_CALLBACK_IF_NEW(instance);

    instance->decode_data_low = 0;
    instance->decode_data_high = 0;
    instance->decode_count_bit = 0;
    instance->am_bits_len = 0;
    instance->state = PSADecoderState0;
    return true;
}

void subghz_protocol_decoder_psa_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderPSA* instance = context;
    if(instance->am_pulse_count < 0xFFFFU) instance->am_pulse_count++;
    if(duration >= PSA_AM_LONG_MIN && duration <= PSA_AM_LONG_MAX &&
       instance->am_long_count < 0xFFFFU)
        instance->am_long_count++;

    uint32_t tolerance;
    uint32_t new_state = instance->state;
    uint32_t prev_dur = instance->prev_duration;
    uint32_t te_short = subghz_protocol_psa_const.te_short;
    uint32_t te_long = subghz_protocol_psa_const.te_long;

    switch(instance->state) {
    case PSADecoderState0:
        if(!level) {
            return;
        }

        if(duration < te_short) {
            tolerance = te_short - duration;
            if(tolerance > PSA_TOLERANCE_99) {
                if(duration < PSA_TE_SHORT_125) {
                    tolerance = PSA_TE_SHORT_125 - duration;
                } else {
                    tolerance = duration - PSA_TE_SHORT_125;
                }
                if(tolerance > PSA_TOLERANCE_49) {
                    return;
                }
                new_state = PSADecoderState3;
            } else {
                new_state = PSADecoderState1;
            }
        } else {
            tolerance = duration - te_short;
            if(tolerance > PSA_TOLERANCE_99) {
                return;
            }
            new_state = PSADecoderState1;
        }

        instance->decode_data_low = 0;
        instance->decode_data_high = 0;
        instance->pattern_counter = 0;
        instance->decode_count_bit = 0;
        instance->mode_serialize = 0;
        instance->te_sum = duration;
        instance->te_count = 1;
        instance->te_detected = 0;
        instance->prev_duration = duration;
        manchester_advance(instance->manchester_state, ManchesterEventReset,
                         &instance->manchester_state, NULL);
        break;

    case PSADecoderState1:
        if(level) {
            return;
        }

        if(duration < te_short) {
            tolerance = te_short - duration;
            if(tolerance < PSA_TOLERANCE_100) {
                uint32_t prev_diff = psa_abs_diff(prev_dur, te_short);
                if(prev_diff <= PSA_TOLERANCE_99) {
                    instance->pattern_counter++;
                }
                instance->prev_duration = duration;
                return;
            }
        } else {
            tolerance = duration - te_short;
            if(tolerance < PSA_TOLERANCE_100) {
                uint32_t prev_diff = psa_abs_diff(prev_dur, te_short);
                if(prev_diff <= PSA_TOLERANCE_99) {
                    instance->pattern_counter++;
                }
                instance->prev_duration = duration;
                return;
            } else {
                uint32_t long_diff;
                if(duration < te_long) {
                    long_diff = te_long - duration;
                } else {
                    long_diff = duration - te_long;
                }
                if(long_diff < 100) {
                    if(instance->pattern_counter > PSA_PATTERN_THRESHOLD_1) {
                        new_state = PSADecoderState2;
                        instance->decode_data_low = 0;
                        instance->decode_data_high = 0;
                        instance->decode_count_bit = 0;
                        manchester_advance(instance->manchester_state, ManchesterEventReset,
                                         &instance->manchester_state, NULL);
                        instance->state = new_state;
                    }
                    instance->pattern_counter = 0;
                    instance->prev_duration = duration;
                    return;
                }
            }
        }

        new_state = PSADecoderState0;
        instance->pattern_counter = 0;
        break;

    case PSADecoderState2:
        if(instance->decode_count_bit >= PSA_MAX_BITS) {
            new_state = PSADecoderState0;
            break;
        }

        if(level && instance->decode_count_bit == PSA_KEY2_BITS) {
            if(duration >= 800) {
                uint32_t end_diff;
                if(duration < PSA_TE_END_1000) {
                    end_diff = PSA_TE_END_1000 - duration;
                } else {
                    end_diff = duration - PSA_TE_END_1000;
                }
                if(end_diff <= 199) {
                    instance->validation_field = (uint16_t)(instance->decode_data_low & 0xFFFF);
                    instance->key2_low = instance->decode_data_low;
                    instance->key2_high = instance->decode_data_high;
                    instance->mode_serialize = 1;
                    instance->status_flag = 0x80;

                    uint8_t buffer[48] = {0};
                    psa_setup_byte_buffer(buffer, instance->key1_low, instance->key1_high, instance->key2_low);
                    if(psa_direct_xor_decrypt(instance, buffer)) {
                        instance->mode_serialize = 0x23;
                        instance->decrypted = 0x50;
                    } else {
                        instance->decrypted = 0x00;
                        instance->mode_serialize = 0x36;
                    }

                    if(instance->decrypted != 0x50 &&
                       ((instance->key1_high >> 16) & 0xF) != 0xA) {
                        instance->decode_data_low = 0;
                        instance->decode_data_high = 0;
                        instance->decode_count_bit = 0;
                        new_state = PSADecoderState0;
                        instance->state = new_state;
                        return;
                    }

                    instance->generic.data = ((uint64_t)instance->key1_high << 32) | instance->key1_low;
                    instance->generic.data_count_bit = 64;
                    instance->decoder.decode_data = instance->generic.data;
                    instance->decoder.decode_count_bit = 64;

                    PSA_FIRE_CALLBACK_IF_NEW(instance);

                    instance->decode_data_low = 0;
                    instance->decode_data_high = 0;
                    instance->decode_count_bit = 0;
                    new_state = PSADecoderState0;
                    instance->state = new_state;
                    return;
                }
            }
        }

        uint8_t manchester_input = 0;
        bool should_process = false;

        if(duration < te_short) {
            tolerance = te_short - duration;
            if(tolerance >= PSA_TOLERANCE_100) {
                return;
            }
            manchester_input = ((level ^ 1) & 0x7f) << 1;
            should_process = true;
        } else {
            tolerance = duration - te_short;
            if(tolerance < PSA_TOLERANCE_100) {
                manchester_input = ((level ^ 1) & 0x7f) << 1;
                should_process = true;
            } else if(duration < te_long) {
                uint32_t diff_from_250 = duration - te_short;
                uint32_t diff_from_500 = te_long - duration;

                if(diff_from_500 < 150 || diff_from_250 > diff_from_500) {
                    if(level == 0) {
                        manchester_input = 6;
                    } else {
                        manchester_input = 4;
                    }
                    should_process = true;
                } else if(diff_from_250 < 150) {
                    manchester_input = ((level ^ 1) & 0x7f) << 1;
                    should_process = true;
                } else {
                    if(duration > 10000) {
                        new_state = PSADecoderState0;
                        instance->pattern_counter = 0;
                        return;
                    }
                    if(duration >= 350 && duration <= 400) {
                        if(level == 0) {
                            manchester_input = 6;
                        } else {
                            manchester_input = 4;
                        }
                        should_process = true;
                    } else {
                        return;
                    }
                }
            } else {
                uint32_t long_diff = duration - te_long;
                if(long_diff < 100) {
                    if(level == 0) {
                        manchester_input = 6;
                    } else {
                        manchester_input = 4;
                    }
                    should_process = true;
                } else {
                    if(!level) {
                        if(duration > 10000) {
                            new_state = PSADecoderState0;
                            instance->pattern_counter = 0;
                            return;
                        }
                        return;
                    }
                    should_process = false;
                }
            }
        }

        if(should_process && instance->decode_count_bit < PSA_KEY2_BITS) {
            bool decoded_bit = false;

            if(manchester_advance(instance->manchester_state,
                               (ManchesterEvent)manchester_input,
                               &instance->manchester_state,
                               &decoded_bit)) {
                uint32_t carry = (instance->decode_data_low >> 31) & 1;
                instance->decode_data_low = (instance->decode_data_low << 1) | (decoded_bit ? 1 : 0);
                instance->decode_data_high = (instance->decode_data_high << 1) | carry;
                instance->decode_count_bit++;

                if(instance->decode_count_bit == PSA_KEY1_BITS) {
                    instance->key1_low = instance->decode_data_low;
                    instance->key1_high = instance->decode_data_high;
                    instance->decode_data_low = 0;
                    instance->decode_data_high = 0;
                }
            }
        }

        if(!level) {
            return;
        }

        if(!should_process) {
            uint32_t end_diff;
            if(duration < PSA_TE_END_1000) {
                end_diff = PSA_TE_END_1000 - duration;
            } else {
                end_diff = duration - PSA_TE_END_1000;
            }
            if(end_diff <= 199) {
                if(instance->decode_count_bit != PSA_KEY2_BITS) {
                    return;
                }

                instance->validation_field = (uint16_t)(instance->decode_data_low & 0xFFFF);

                if(((instance->key1_high >> 16) & 0xF) == 0xA) {
                    instance->key2_low = instance->decode_data_low;
                    instance->key2_high = instance->decode_data_high;
                    instance->mode_serialize = 1;
                    instance->status_flag = 0x80;

                    uint8_t buffer[48] = {0};
                    psa_setup_byte_buffer(buffer, instance->key1_low, instance->key1_high, instance->key2_low);
                    if(psa_direct_xor_decrypt(instance, buffer)) {
                        instance->mode_serialize = 0x23;
                        instance->decrypted = 0x50;
                    } else {
                        instance->decrypted = 0x00;
                        instance->mode_serialize = 0x36;
                    }

                    instance->generic.data = ((uint64_t)instance->key1_high << 32) | instance->key1_low;
                    instance->generic.data_count_bit = 64;
                    instance->decoder.decode_data = instance->generic.data;
                    instance->decoder.decode_count_bit = 64;

                    PSA_FIRE_CALLBACK_IF_NEW(instance);

                    instance->decode_data_low = 0;
                    instance->decode_data_high = 0;
                    instance->decode_count_bit = 0;
                    new_state = PSADecoderState0;
                } else {
                    return;
                }
            } else {
                return;
            }
        }
        break;

    case PSADecoderState3: {
        if(level) {
            instance->prev_duration = duration;
            return;
        }
        uint32_t high = prev_dur;
        uint32_t period = high + duration;
        uint32_t te_avg3 =
            (instance->te_count > 0) ? (instance->te_sum / instance->te_count) : PSA_TE_SHORT_125;
        if(te_avg3 < PSA_AM_PRE_MIN) te_avg3 = PSA_TE_SHORT_125;
        uint32_t period_est = te_avg3 * 2;
        uint32_t cell_mid = period_est + (period_est >> 1);
        uint32_t cell_hi = period_est + period_est + (period_est >> 1);
        uint32_t cell_min = PSA_AM_PRE_MIN * 2;
        bool is_cell =
            (period >= cell_min && period <= cell_mid && high >= PSA_AM_PRE_MIN &&
             high <= PSA_AM_PRE_MAX && duration >= PSA_AM_PRE_MIN && duration <= PSA_AM_PRE_MAX);
        bool is_boundary = (!is_cell && period > cell_mid && period <= cell_hi);

        if(is_cell) {
            instance->pattern_counter++;
            instance->pre_glitch = 0;
            instance->te_sum += (period >> 1);
            instance->te_count++;
            if(instance->te_count > 32) {
                instance->te_sum = te_avg3 * 8;
                instance->te_count = 8;
            }
            instance->prev_duration = duration;
            return;
        }

        if(is_boundary && instance->pattern_counter >= PSA_AM_PRE_THRESHOLD) {
            instance->te_detected = te_avg3;
            instance->am_long_count = 0;
            instance->am_pulse_count = 0;
            instance->decode_data_low = 0;
            instance->decode_data_high = 0;
            instance->decode_count_bit = 0;
            instance->am_bits_len = 0;
            manchester_advance(
                instance->manchester_state, ManchesterEventReset, &instance->manchester_state, NULL);
            instance->am_await_high = 0;
            instance->state = PSADecoderState4;
            instance->pattern_counter = 0;
            instance->pre_glitch = 0;
            instance->prev_duration = duration;
            return;
        }

        if(instance->pattern_counter >= PSA_AM_PRE_THRESHOLD) {
            instance->te_detected = te_avg3;
            instance->am_long_count = 0;
            instance->am_pulse_count = 0;
            instance->decode_data_low = 0;
            instance->decode_data_high = 0;
            instance->decode_count_bit = 0;
            instance->am_bits_len = 0;
            manchester_advance(
                instance->manchester_state, ManchesterEventReset, &instance->manchester_state, NULL);
            instance->am_await_high = 1;
            instance->state = PSADecoderState4;
            instance->pattern_counter = 0;
            instance->pre_glitch = 0;
            instance->prev_duration = duration;
            return;
        }

        instance->pre_glitch++;
        if(instance->pattern_counter >= PSA_AM_PRE_GLITCH_DECAY) {
            instance->pattern_counter -= PSA_AM_PRE_GLITCH_DECAY;
        } else {
            instance->pattern_counter = 0;
        }
        if(instance->pre_glitch <= PSA_AM_PRE_MAX_GLITCH && instance->pattern_counter > 0) {
            instance->prev_duration = duration;
            return;
        }
        new_state = PSADecoderState0;
        instance->pattern_counter = 0;
        instance->pre_glitch = 0;
        break;
    }

    case PSADecoderState4: {
        uint32_t te_s = instance->te_detected ? instance->te_detected : PSA_TE_SHORT_125;
        uint32_t te_l = te_s * 2;
        uint32_t te_tol = (te_s * 7) / 10;
        uint32_t midpoint = te_s + (te_s >> 1);

        if(instance->am_await_high) {
            if(duration > te_l + te_tol) {
                instance->prev_duration = duration;
                return;
            }
            instance->am_await_high = 0;
            manchester_advance(
                instance->manchester_state, ManchesterEventReset, &instance->manchester_state, NULL);
            instance->decode_data_low = 0;
            instance->decode_data_high = 0;
            instance->decode_count_bit = 0;
            instance->am_bits_len = 0;
        }

        if(instance->decode_count_bit >= PSA_KEY2_BITS && duration > te_l + te_tol) {
            if(psa_am_complete(instance)) return;
        }

        if(instance->decode_count_bit >= PSA_MAX_BITS) {
            if(!psa_am_complete(instance)) {
                new_state = PSADecoderState0;
            }
            if(instance->state == PSADecoderState0) break;
            return;
        }

        if(duration > te_l + te_tol) {
            if(instance->decode_count_bit >= PSA_KEY2_BITS) {
                if(psa_am_complete(instance)) return;
            }
            if(instance->decode_count_bit < PSA_AM_RESYNC_BITS) {
                instance->decode_data_low = 0;
                instance->decode_data_high = 0;
                instance->decode_count_bit = 0;
                instance->am_bits_len = 0;
                manchester_advance(
                    instance->manchester_state, ManchesterEventReset, &instance->manchester_state,
                    NULL);
                return;
            }
            if(duration > 10000) {
                new_state = PSADecoderState0;
                instance->am_bits_len = 0;
                break;
            }
            return;
        }

        uint8_t manchester_input4;
        if(duration <= midpoint) {
            if(psa_abs_diff(duration, te_s) > te_tol) {
                if(instance->decode_count_bit < PSA_AM_RESYNC_BITS) {
                    instance->decode_data_low = 0;
                    instance->decode_data_high = 0;
                    instance->decode_count_bit = 0;
                    instance->am_bits_len = 0;
                    manchester_advance(
                        instance->manchester_state, ManchesterEventReset,
                        &instance->manchester_state, NULL);
                }
                return;
            }
            manchester_input4 = level ? ManchesterEventShortLow : ManchesterEventShortHigh;
        } else {
            if(psa_abs_diff(duration, te_l) > te_tol) {
                if(instance->decode_count_bit < PSA_AM_RESYNC_BITS) {
                    instance->decode_data_low = 0;
                    instance->decode_data_high = 0;
                    instance->decode_count_bit = 0;
                    instance->am_bits_len = 0;
                    manchester_advance(
                        instance->manchester_state, ManchesterEventReset,
                        &instance->manchester_state, NULL);
                }
                return;
            }
            manchester_input4 = level ? ManchesterEventLongLow : ManchesterEventLongHigh;
        }

        bool decoded_bit4 = false;
        if(manchester_advance(
               instance->manchester_state,
               (ManchesterEvent)manchester_input4,
               &instance->manchester_state,
               &decoded_bit4)) {
            psa_add_am_bit(instance, decoded_bit4 ? 1 : 0);
        }
        break;
    }
    }

    if(new_state == PSADecoderState0) instance->am_bits_len = 0;

    instance->state = new_state;
    instance->prev_duration = duration;
}

SubGhzProtocolStatus subghz_protocol_decoder_psa_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderPSA* instance = context;

    if(instance->decrypted != 0x50 && instance->status_flag == 0x80) {
        psa_decrypt_fast(instance);

        if(instance->decrypted == 0x50) {
            instance->generic.cnt = instance->decrypted_counter;
            instance->generic.serial = instance->decrypted_serial;
            instance->generic.btn = instance->decrypted_button;
        }
    }

    instance->generic.data_count_bit = 128;

    SubGhzProtocolStatus ret = subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }

    do {
        char key2_str[32];
        snprintf(key2_str, sizeof(key2_str),
                 "%02X %02X %02X %02X %02X %02X %02X %02X",
                 (unsigned int)((instance->key2_high >> 24) & 0xFF),
                 (unsigned int)((instance->key2_high >> 16) & 0xFF),
                 (unsigned int)((instance->key2_high >> 8) & 0xFF),
                 (unsigned int)(instance->key2_high & 0xFF),
                 (unsigned int)((instance->key2_low >> 24) & 0xFF),
                 (unsigned int)((instance->key2_low >> 16) & 0xFF),
                 (unsigned int)((instance->key2_low >> 8) & 0xFF),
                 (unsigned int)(instance->key2_low & 0xFF));
        if(!flipper_format_write_string_cstr(flipper_format, "Key_2", key2_str)) {
            ret = SubGhzProtocolStatusError;
            break;
        }

        if(instance->decrypted == 0x50 && instance->decrypted_type != 0x00) {
            char serial_str[16];
            snprintf(serial_str, sizeof(serial_str), "%02X %02X %02X",
                     (unsigned int)((instance->decrypted_serial >> 16) & 0xFF),
                     (unsigned int)((instance->decrypted_serial >> 8) & 0xFF),
                     (unsigned int)(instance->decrypted_serial & 0xFF));
            if(!flipper_format_write_string_cstr(flipper_format, "Serial", serial_str)) {
                ret = SubGhzProtocolStatusError;
                break;
            }

            char cnt_str[24];
            if(instance->decrypted_type == 0x23) {
                snprintf(cnt_str, sizeof(cnt_str), "%02X %02X",
                         (unsigned int)((instance->decrypted_counter >> 8) & 0xFF),
                         (unsigned int)(instance->decrypted_counter & 0xFF));
            } else {
                snprintf(cnt_str, sizeof(cnt_str), "%02X %02X %02X %02X",
                         (unsigned int)((instance->decrypted_counter >> 24) & 0x0F),
                         (unsigned int)((instance->decrypted_counter >> 16) & 0xFF),
                         (unsigned int)((instance->decrypted_counter >> 8) & 0xFF),
                         (unsigned int)(instance->decrypted_counter & 0xFF));
            }
            if(!flipper_format_write_string_cstr(flipper_format, "Cnt", cnt_str)) {
                ret = SubGhzProtocolStatusError;
                break;
            }

            char btn_str[8];
            snprintf(btn_str, sizeof(btn_str), "%02X", (unsigned int)instance->decrypted_button);
            if(!flipper_format_write_string_cstr(flipper_format, "Btn", btn_str)) {
                ret = SubGhzProtocolStatusError;
                break;
            }

            char type_str[8];
            snprintf(type_str, sizeof(type_str), "%02X", (unsigned int)instance->decrypted_type);
            if(!flipper_format_write_string_cstr(flipper_format, "Type", type_str)) {
                ret = SubGhzProtocolStatusError;
                break;
            }

            char crc_str[12];
            if(instance->decrypted_type == 0x23) {
                snprintf(crc_str, sizeof(crc_str), "%02X", (unsigned int)(instance->decrypted_crc & 0xFF));
            } else {
                snprintf(crc_str, sizeof(crc_str), "%02X %02X",
                         (unsigned int)((instance->decrypted_crc >> 8) & 0xFF),
                         (unsigned int)(instance->decrypted_crc & 0xFF));
            }
            if(!flipper_format_write_string_cstr(flipper_format, "CRC", crc_str)) {
                ret = SubGhzProtocolStatusError;
                break;
            }

            char seed_str[16];
            snprintf(seed_str, sizeof(seed_str), "%02X %02X %02X",
                     (unsigned int)((instance->decrypted_seed >> 16) & 0xFF),
                     (unsigned int)((instance->decrypted_seed >> 8) & 0xFF),
                     (unsigned int)(instance->decrypted_seed & 0xFF));
            if(!flipper_format_write_string_cstr(flipper_format, "Seed", seed_str)) {
                ret = SubGhzProtocolStatusError;
                break;
            }
        }

        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

SubGhzProtocolStatus subghz_protocol_decoder_psa_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderPSA* instance = context;

    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    FuriString* temp_str = furi_string_alloc();

    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }

        uint64_t key1 = instance->generic.data;
        instance->key1_low = (uint32_t)(key1 & 0xFFFFFFFF);
        instance->key1_high = (uint32_t)((key1 >> 32) & 0xFFFFFFFF);

        if(!flipper_format_read_string(flipper_format, "Key_2", temp_str)) {
            break;
        }

        const char* key2_str = furi_string_get_cstr(temp_str);
        uint64_t key2 = 0;
        for(size_t i = 0; i < strlen(key2_str); i++) {
            char c = key2_str[i];
            if(c == ' ') continue;

            uint8_t nibble;
            if(c >= '0' && c <= '9') {
                nibble = c - '0';
            } else if(c >= 'A' && c <= 'F') {
                nibble = c - 'A' + 10;
            } else if(c >= 'a' && c <= 'f') {
                nibble = c - 'a' + 10;
            } else {
                break;
            }
            key2 = (key2 << 4) | nibble;
        }
        instance->key2_low = (uint32_t)(key2 & 0xFFFFFFFF);
        instance->key2_high = (uint32_t)((key2 >> 32) & 0xFFFFFFFF);

        instance->generic.data = ((uint64_t)instance->key1_high << 32) | instance->key1_low;
        instance->generic.data_count_bit = 128;
        instance->status_flag = 0x80;

        uint32_t serial = 0;
        uint32_t counter = 0;
        uint8_t button = 0;
        uint8_t type = 0;
        uint16_t crc = 0;
        uint32_t seed = 0;

        bool has_decrypted_data = true;

        if(flipper_format_read_string(flipper_format, "Serial", temp_str)) {
            const char* serial_str = furi_string_get_cstr(temp_str);
            for(size_t i = 0; i < strlen(serial_str); i++) {
                char c = serial_str[i];
                if(c == ' ') continue;
                uint8_t nibble = (c >= '0' && c <= '9') ? c - '0' :
                               (c >= 'A' && c <= 'F') ? c - 'A' + 10 :
                               (c >= 'a' && c <= 'f') ? c - 'a' + 10 : 0;
                serial = (serial << 4) | nibble;
            }
        } else {
            has_decrypted_data = false;
        }

        if(flipper_format_read_string(flipper_format, "Cnt", temp_str)) {
            const char* cnt_str = furi_string_get_cstr(temp_str);
            for(size_t i = 0; i < strlen(cnt_str); i++) {
                char c = cnt_str[i];
                if(c == ' ') continue;
                uint8_t nibble = (c >= '0' && c <= '9') ? c - '0' :
                               (c >= 'A' && c <= 'F') ? c - 'A' + 10 :
                               (c >= 'a' && c <= 'f') ? c - 'a' + 10 : 0;
                counter = (counter << 4) | nibble;
            }
        } else {
            has_decrypted_data = false;
        }

        if(flipper_format_read_string(flipper_format, "Btn", temp_str)) {
            const char* btn_str = furi_string_get_cstr(temp_str);
            uint32_t btn_val = 0;
            for(size_t i = 0; i < strlen(btn_str); i++) {
                char c = btn_str[i];
                if(c == ' ') continue;
                uint8_t nibble = (c >= '0' && c <= '9') ? c - '0' :
                               (c >= 'A' && c <= 'F') ? c - 'A' + 10 :
                               (c >= 'a' && c <= 'f') ? c - 'a' + 10 : 0;
                btn_val = (btn_val << 4) | nibble;
            }
            button = (uint8_t)(btn_val & 0xFF);
        } else {
            has_decrypted_data = false;
        }

        if(flipper_format_read_string(flipper_format, "Type", temp_str)) {
            const char* type_str = furi_string_get_cstr(temp_str);
            uint32_t type_val = 0;
            for(size_t i = 0; i < strlen(type_str); i++) {
                char c = type_str[i];
                if(c == ' ') continue;
                uint8_t nibble = (c >= '0' && c <= '9') ? c - '0' :
                               (c >= 'A' && c <= 'F') ? c - 'A' + 10 :
                               (c >= 'a' && c <= 'f') ? c - 'a' + 10 : 0;
                type_val = (type_val << 4) | nibble;
            }
            type = (uint8_t)(type_val & 0xFF);
        } else {
            has_decrypted_data = false;
        }

        if(flipper_format_read_string(flipper_format, "CRC", temp_str)) {
            const char* crc_str = furi_string_get_cstr(temp_str);
            uint32_t crc_val = 0;
            for(size_t i = 0; i < strlen(crc_str); i++) {
                char c = crc_str[i];
                if(c == ' ') continue;
                uint8_t nibble = (c >= '0' && c <= '9') ? c - '0' :
                               (c >= 'A' && c <= 'F') ? c - 'A' + 10 :
                               (c >= 'a' && c <= 'f') ? c - 'a' + 10 : 0;
                crc_val = (crc_val << 4) | nibble;
            }
            crc = (uint16_t)(crc_val & 0xFFFF);
        } else {
            has_decrypted_data = false;
        }

        if(flipper_format_read_string(flipper_format, "Seed", temp_str)) {
            const char* seed_str = furi_string_get_cstr(temp_str);
            for(size_t i = 0; i < strlen(seed_str); i++) {
                char c = seed_str[i];
                if(c == ' ') continue;
                uint8_t nibble = (c >= '0' && c <= '9') ? c - '0' :
                               (c >= 'A' && c <= 'F') ? c - 'A' + 10 :
                               (c >= 'a' && c <= 'f') ? c - 'a' + 10 : 0;
                seed = (seed << 4) | nibble;
            }
        }

        if(has_decrypted_data && type != 0x00) {
            instance->decrypted_serial = serial;
            instance->decrypted_counter = counter;
            instance->decrypted_button = button;
            instance->decrypted_type = type;
            instance->decrypted_crc = crc;
            instance->decrypted_seed = seed;
            instance->decrypted = 0x50;

            instance->generic.cnt = counter;
            instance->generic.serial = serial;
            instance->generic.btn = button;
        } else {
            psa_decrypt_fast(instance);

            instance->generic.cnt = instance->decrypted_counter;
            instance->generic.serial = instance->decrypted_serial;
            instance->generic.btn = instance->decrypted_button;
        }

        ret = SubGhzProtocolStatusOk;
    } while(false);

    furi_string_free(temp_str);
    return ret;
}

bool subghz_protocol_psa_decrypt_file(FlipperFormat* flipper_format, FuriString* result_str, PsaDecryptProgressCallback progress_cb, void* progress_ctx) {
    SubGhzProtocolDecoderPSA instance = {0};

    uint8_t key1_bytes[8] = {0};
    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(flipper_format, "Key", key1_bytes, 8)) return false;
    instance.key1_high = ((uint32_t)key1_bytes[0] << 24) | ((uint32_t)key1_bytes[1] << 16) |
                         ((uint32_t)key1_bytes[2] << 8) | key1_bytes[3];
    instance.key1_low  = ((uint32_t)key1_bytes[4] << 24) | ((uint32_t)key1_bytes[5] << 16) |
                         ((uint32_t)key1_bytes[6] << 8) | key1_bytes[7];

    FuriString* temp = furi_string_alloc();
    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_string(flipper_format, "Key_2", temp)) {
        furi_string_free(temp);
        return false;
    }
    const char* k2 = furi_string_get_cstr(temp);
    uint64_t key2 = 0;
    for(size_t i = 0; i < strlen(k2); i++) {
        char c = k2[i]; if(c == ' ') continue;
        uint8_t n = (c>='0'&&c<='9') ? c-'0' : (c>='A'&&c<='F') ? c-'A'+10 : (c>='a'&&c<='f') ? c-'a'+10 : 0;
        key2 = (key2 << 4) | n;
    }
    furi_string_free(temp);
    instance.key2_low  = (uint32_t)(key2 & 0xFFFFFFFF);
    instance.key2_high = (uint32_t)(key2 >> 32);
    instance.status_flag = 0x80;
    instance.mode_serialize = 0;

    psa_decrypt_full(&instance, progress_cb, progress_ctx);

    if(instance.decrypted != 0x50 || instance.decrypted_type == 0) return false;

    flipper_format_rewind(flipper_format);
    char serial_str[16];
    snprintf(serial_str, sizeof(serial_str), "%02X %02X %02X",
        (unsigned int)((instance.decrypted_serial >> 16) & 0xFF),
        (unsigned int)((instance.decrypted_serial >> 8) & 0xFF),
        (unsigned int)(instance.decrypted_serial & 0xFF));
    flipper_format_insert_or_update_string_cstr(flipper_format, "Serial", serial_str);

    flipper_format_rewind(flipper_format);
    char cnt_str[24];
    if(instance.decrypted_type == 0x23) {
        snprintf(cnt_str, sizeof(cnt_str), "%02X %02X",
            (unsigned int)((instance.decrypted_counter >> 8) & 0xFF),
            (unsigned int)(instance.decrypted_counter & 0xFF));
    } else {
        snprintf(cnt_str, sizeof(cnt_str), "%02X %02X %02X %02X",
            (unsigned int)((instance.decrypted_counter >> 24) & 0x0F),
            (unsigned int)((instance.decrypted_counter >> 16) & 0xFF),
            (unsigned int)((instance.decrypted_counter >> 8) & 0xFF),
            (unsigned int)(instance.decrypted_counter & 0xFF));
    }
    flipper_format_insert_or_update_string_cstr(flipper_format, "Cnt", cnt_str);

    flipper_format_rewind(flipper_format);
    char btn_str[8];
    snprintf(btn_str, sizeof(btn_str), "%02X", (unsigned int)instance.decrypted_button);
    flipper_format_insert_or_update_string_cstr(flipper_format, "Btn", btn_str);

    flipper_format_rewind(flipper_format);
    char type_str[8];
    snprintf(type_str, sizeof(type_str), "%02X", (unsigned int)instance.decrypted_type);
    flipper_format_insert_or_update_string_cstr(flipper_format, "Type", type_str);

    flipper_format_rewind(flipper_format);
    char crc_str[12];
    if(instance.decrypted_type == 0x23) {
        snprintf(crc_str, sizeof(crc_str), "%02X", (unsigned int)(instance.decrypted_crc & 0xFF));
    } else {
        snprintf(crc_str, sizeof(crc_str), "%02X %02X",
            (unsigned int)((instance.decrypted_crc >> 8) & 0xFF),
            (unsigned int)(instance.decrypted_crc & 0xFF));
    }
    flipper_format_insert_or_update_string_cstr(flipper_format, "CRC", crc_str);

    flipper_format_rewind(flipper_format);
    char seed_str[16];
    snprintf(seed_str, sizeof(seed_str), "%02X %02X %02X",
        (unsigned int)((instance.decrypted_serial >> 16) & 0xFF),
        (unsigned int)((instance.decrypted_serial >> 8) & 0xFF),
        (unsigned int)(instance.decrypted_serial & 0xFF));
    flipper_format_insert_or_update_string_cstr(flipper_format, "Seed", seed_str);

    if(result_str != NULL) {
        furi_string_printf(result_str,
            "Type: %02X\nSeed: %08lX",
            instance.decrypted_type,
            instance.decrypted_seed);
    }

    return true;
}

bool subghz_protocol_psa_get_bf_params(
    FlipperFormat* flipper_format,
    uint32_t* w0,
    uint32_t* w1) {
    SubGhzProtocolDecoderPSA instance = {0};

    uint8_t key1_bytes[8] = {0};
    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(flipper_format, "Key", key1_bytes, 8)) return false;
    instance.key1_high = ((uint32_t)key1_bytes[0] << 24) | ((uint32_t)key1_bytes[1] << 16) |
                         ((uint32_t)key1_bytes[2] << 8) | key1_bytes[3];
    instance.key1_low  = ((uint32_t)key1_bytes[4] << 24) | ((uint32_t)key1_bytes[5] << 16) |
                         ((uint32_t)key1_bytes[6] << 8) | key1_bytes[7];

    FuriString* temp = furi_string_alloc();
    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_string(flipper_format, "Key_2", temp)) {
        furi_string_free(temp);
        return false;
    }
    const char* k2 = furi_string_get_cstr(temp);
    uint64_t key2 = 0;
    for(size_t i = 0; i < strlen(k2); i++) {
        char c = k2[i]; if(c == ' ') continue;
        uint8_t n = (c>='0'&&c<='9') ? c-'0' : (c>='A'&&c<='F') ? c-'A'+10 : (c>='a'&&c<='f') ? c-'a'+10 : 0;
        key2 = (key2 << 4) | n;
    }
    furi_string_free(temp);
    instance.key2_low = (uint32_t)(key2 & 0xFFFFFFFF);

    uint8_t buffer[48] = {0};
    psa_setup_byte_buffer(buffer, instance.key1_low, instance.key1_high, instance.key2_low);
    if(psa_direct_xor_decrypt(&instance, buffer)) return false;

    psa_prepare_tea_data(buffer, w0, w1);
    return true;
}

bool subghz_protocol_psa_apply_bf_result(
    FlipperFormat* flipper_format,
    FuriString* result_str,
    uint32_t counter,
    uint32_t dec_v0,
    uint32_t dec_v1,
    int bf_type) {
    UNUSED(bf_type);

    SubGhzProtocolDecoderPSA instance = {0};

    uint8_t buffer[48] = {0};
    psa_unpack_tea_result_to_buffer(buffer, dec_v0, dec_v1);
    psa_extract_fields_mode36(buffer, &instance);
    instance.decrypted_seed = counter;
    instance.decrypted_type = 0x36;

    flipper_format_rewind(flipper_format);
    char serial_str[16];
    snprintf(serial_str, sizeof(serial_str), "%02X %02X %02X",
        (unsigned int)((instance.decrypted_serial >> 16) & 0xFF),
        (unsigned int)((instance.decrypted_serial >> 8) & 0xFF),
        (unsigned int)(instance.decrypted_serial & 0xFF));
    flipper_format_insert_or_update_string_cstr(flipper_format, "Serial", serial_str);

    flipper_format_rewind(flipper_format);
    char cnt_str[24];
    snprintf(cnt_str, sizeof(cnt_str), "%02X %02X %02X %02X",
        (unsigned int)((instance.decrypted_counter >> 24) & 0x0F),
        (unsigned int)((instance.decrypted_counter >> 16) & 0xFF),
        (unsigned int)((instance.decrypted_counter >> 8) & 0xFF),
        (unsigned int)(instance.decrypted_counter & 0xFF));
    flipper_format_insert_or_update_string_cstr(flipper_format, "Cnt", cnt_str);

    flipper_format_rewind(flipper_format);
    char btn_str[8];
    snprintf(btn_str, sizeof(btn_str), "%02X", (unsigned int)instance.decrypted_button);
    flipper_format_insert_or_update_string_cstr(flipper_format, "Btn", btn_str);

    flipper_format_rewind(flipper_format);
    flipper_format_insert_or_update_string_cstr(flipper_format, "Type", "36");

    flipper_format_rewind(flipper_format);
    char crc_str[12];
    snprintf(crc_str, sizeof(crc_str), "%02X %02X",
        (unsigned int)((instance.decrypted_crc >> 8) & 0xFF),
        (unsigned int)(instance.decrypted_crc & 0xFF));
    flipper_format_insert_or_update_string_cstr(flipper_format, "CRC", crc_str);

    flipper_format_rewind(flipper_format);
    char seed_str[16];
    snprintf(seed_str, sizeof(seed_str), "%02X %02X %02X",
        (unsigned int)((instance.decrypted_serial >> 16) & 0xFF),
        (unsigned int)((instance.decrypted_serial >> 8) & 0xFF),
        (unsigned int)(instance.decrypted_serial & 0xFF));
    flipper_format_insert_or_update_string_cstr(flipper_format, "Seed", seed_str);

    if(result_str != NULL) {
        furi_string_printf(result_str,
            "Decrypted (ext)!\nType: 36\nKey: %08lX",
            counter);
    }
    return true;
}
