/* decode_kia_v6.fal : SD get_string for kia_v6 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/kia_v6.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/toolbox/manchester_decoder.h>
#include <string.h>

#define KIA_V6_XOR_MASK_LOW  0x84AF25FB
#define KIA_V6_XOR_MASK_HIGH 0x638766AB
#define kia_v6_crc8(data, len) subghz_protocol_blocks_crc8((data), (len), 0x07, 0xFF)

struct SubGhzProtocolDecoderKiaV6 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;

    ManchesterState manchester_state;

    uint32_t data_part1_low;
    uint32_t data_part1_high;

    uint32_t stored_part1_low;
    uint32_t stored_part1_high;
    uint32_t stored_part2_low;
    uint32_t stored_part2_high;
    uint16_t data_part3;

    uint8_t bit_count;
    uint8_t fx_field;
    uint8_t crc1_field;
    uint8_t crc2_field;
};

static const uint8_t aes_sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab,
    0x76, 0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4,
    0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71,
    0xd8, 0x31, 0x15, 0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2,
    0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6,
    0xb3, 0x29, 0xe3, 0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb,
    0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf, 0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45,
    0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8, 0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
    0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44,
    0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73, 0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a,
    0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32, 0x3a, 0x0a, 0x49,
    0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d,
    0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08, 0xba, 0x78, 0x25,
    0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a, 0x70, 0x3e,
    0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e, 0xe1,
    0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb,
    0x16};

static const uint8_t aes_sbox_inv[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7,
    0xfb, 0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde,
    0xe9, 0xcb, 0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42,
    0xfa, 0xc3, 0x4e, 0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49,
    0x6d, 0x8b, 0xd1, 0x25, 0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c,
    0xcc, 0x5d, 0x65, 0xb6, 0x92, 0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15,
    0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84, 0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7,
    0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06, 0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02,
    0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b, 0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc,
    0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73, 0x96, 0xac, 0x74, 0x22, 0xe7, 0xad,
    0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e, 0x47, 0xf1, 0x1a, 0x71, 0x1d,
    0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b, 0xfc, 0x56, 0x3e, 0x4b,
    0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4, 0x1f, 0xdd, 0xa8,
    0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f, 0x60, 0x51,
    0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef, 0xa0,
    0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c,
    0x7d};

static const uint8_t aes_rcon[10] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36};

static uint8_t gf_mul2(uint8_t x);
static void aes_subbytes_inv(uint8_t* state);
static void aes_shiftrows_inv(uint8_t* state);
static void aes_mixcolumns_inv(uint8_t* state);
static void aes_addroundkey(uint8_t* state, const uint8_t* round_key);
static void aes_key_expansion(const uint8_t* key, uint8_t* round_keys);
static void aes128_decrypt(const uint8_t* expanded_key, uint8_t* data);
static void get_kia_v6_aes_key(uint8_t* aes_key);
static bool kia_v6_decrypt(SubGhzProtocolDecoderKiaV6* instance);

static uint8_t gf_mul2(uint8_t x) {
    return ((x >> 7) * 0x1b) ^ (x << 1);
}

static void aes_subbytes_inv(uint8_t* state) {
    for(int row = 0; row < 4; row++) {
        for(int col = 0; col < 4; col++) {
            state[row + col * 4] = aes_sbox_inv[state[row + col * 4]];
        }
    }
}

static void aes_shiftrows_inv(uint8_t* state) {
    uint8_t temp;

    temp = state[13];
    state[13] = state[9];
    state[9] = state[5];
    state[5] = state[1];
    state[1] = temp;

    temp = state[2];
    state[2] = state[10];
    state[10] = temp;
    temp = state[6];
    state[6] = state[14];
    state[14] = temp;

    temp = state[3];
    state[3] = state[7];
    state[7] = state[11];
    state[11] = state[15];
    state[15] = temp;
}

static void aes_mixcolumns_inv(uint8_t* state) {
    uint8_t a, b, c, d;
    for(int i = 0; i < 4; i++) {
        a = state[i * 4];
        b = state[i * 4 + 1];
        c = state[i * 4 + 2];
        d = state[i * 4 + 3];

        uint8_t a2 = gf_mul2(a);
        uint8_t a4 = gf_mul2(a2);
        uint8_t a8 = gf_mul2(a4);
        uint8_t b2 = gf_mul2(b);
        uint8_t b4 = gf_mul2(b2);
        uint8_t b8 = gf_mul2(b4);
        uint8_t c2 = gf_mul2(c);
        uint8_t c4 = gf_mul2(c2);
        uint8_t c8 = gf_mul2(c4);
        uint8_t d2 = gf_mul2(d);
        uint8_t d4 = gf_mul2(d2);
        uint8_t d8 = gf_mul2(d4);

        state[i * 4] = (a8 ^ a4 ^ a2) ^ (b8 ^ b2 ^ b) ^ (c8 ^ c4 ^ c) ^ (d8 ^ d);
        state[i * 4 + 1] = (a8 ^ a) ^ (b8 ^ b4 ^ b2) ^ (c8 ^ c2 ^ c) ^ (d8 ^ d4 ^ d);
        state[i * 4 + 2] = (a8 ^ a4 ^ a) ^ (b8 ^ b) ^ (c8 ^ c4 ^ c2) ^ (d8 ^ d2 ^ d);
        state[i * 4 + 3] = (a8 ^ a2 ^ a) ^ (b8 ^ b4 ^ b) ^ (c8 ^ c) ^ (d8 ^ d4 ^ d2);
    }
}

static void aes_addroundkey(uint8_t* state, const uint8_t* round_key) {
    for(int col = 0; col < 4; col++) {
        state[col * 4] ^= round_key[col * 4];
        state[col * 4 + 1] ^= round_key[col * 4 + 1];
        state[col * 4 + 2] ^= round_key[col * 4 + 2];
        state[col * 4 + 3] ^= round_key[col * 4 + 3];
    }
}

static void aes_key_expansion(const uint8_t* key, uint8_t* round_keys) {
    for(int i = 0; i < 16; i++) {
        round_keys[i] = key[i];
    }

    for(int i = 4; i < 44; i++) {
        int prev_word_idx = (i - 1) * 4;
        uint8_t b0 = round_keys[prev_word_idx];
        uint8_t b1 = round_keys[prev_word_idx + 1];
        uint8_t b2 = round_keys[prev_word_idx + 2];
        uint8_t b3 = round_keys[prev_word_idx + 3];

        if((i % 4) == 0) {
            uint8_t new_b0 = aes_sbox[b1] ^ aes_rcon[(i / 4) - 1];
            uint8_t new_b1 = aes_sbox[b2];
            uint8_t new_b2 = aes_sbox[b3];
            uint8_t new_b3 = aes_sbox[b0];
            b0 = new_b0;
            b1 = new_b1;
            b2 = new_b2;
            b3 = new_b3;
        }

        int back_word_idx = (i - 4) * 4;
        b0 ^= round_keys[back_word_idx];
        b1 ^= round_keys[back_word_idx + 1];
        b2 ^= round_keys[back_word_idx + 2];
        b3 ^= round_keys[back_word_idx + 3];

        int curr_word_idx = i * 4;
        round_keys[curr_word_idx] = b0;
        round_keys[curr_word_idx + 1] = b1;
        round_keys[curr_word_idx + 2] = b2;
        round_keys[curr_word_idx + 3] = b3;
    }
}

static void aes128_decrypt(const uint8_t* expanded_key, uint8_t* data) {
    uint8_t state[16];
    memcpy(state, data, 16);

    aes_addroundkey(state, &expanded_key[160]);

    for(int round = 9; round > 0; round--) {
        aes_shiftrows_inv(state);
        aes_subbytes_inv(state);
        aes_addroundkey(state, &expanded_key[round * 16]);
        aes_mixcolumns_inv(state);
    }

    aes_shiftrows_inv(state);
    aes_subbytes_inv(state);
    aes_addroundkey(state, &expanded_key[0]);

    memcpy(data, state, 16);
}

static void get_kia_v6_aes_key(uint8_t* aes_key) {
    uint64_t keystore_a = 0x37CE21F8C9F862A8ULL ^ 0x5448455049524154ULL;
    uint32_t keystore_a_hi = (keystore_a >> 32) & 0xFFFFFFFF;
    uint32_t keystore_a_lo = keystore_a & 0xFFFFFFFF;

    uint32_t uVar15_a = keystore_a_lo ^ KIA_V6_XOR_MASK_LOW;
    uint32_t uVar5_a = KIA_V6_XOR_MASK_HIGH ^ keystore_a_hi;

    uint64_t val64_a = ((uint64_t)uVar5_a << 32) | uVar15_a;
    for(int i = 0; i < 8; i++) {
        aes_key[i] = (val64_a >> (56 - i * 8)) & 0xFF;
    }

    uint64_t keystore_b = 0x3FC629F0C1F06AA0ULL ^ 0x5448455049524154ULL;
    uint32_t keystore_b_hi = (keystore_b >> 32) & 0xFFFFFFFF;
    uint32_t keystore_b_lo = keystore_b & 0xFFFFFFFF;

    uint32_t uVar15_b = keystore_b_lo ^ KIA_V6_XOR_MASK_LOW;
    uint32_t uVar5_b = KIA_V6_XOR_MASK_HIGH ^ keystore_b_hi;

    uint64_t val64_b = ((uint64_t)uVar5_b << 32) | uVar15_b;
    for(int i = 0; i < 8; i++) {
        aes_key[i + 8] = (val64_b >> (56 - i * 8)) & 0xFF;
    }
}

static bool kia_v6_decrypt(SubGhzProtocolDecoderKiaV6* instance) {
    uint8_t encrypted_data[16];

    encrypted_data[0] = (instance->stored_part1_high >> 8) & 0xFF;
    encrypted_data[1] = instance->stored_part1_high & 0xFF;

    encrypted_data[2] = (instance->stored_part1_low >> 24) & 0xFF;
    encrypted_data[3] = (instance->stored_part1_low >> 16) & 0xFF;
    encrypted_data[4] = (instance->stored_part1_low >> 8) & 0xFF;
    encrypted_data[5] = instance->stored_part1_low & 0xFF;

    encrypted_data[6] = (instance->stored_part2_high >> 24) & 0xFF;
    encrypted_data[7] = (instance->stored_part2_high >> 16) & 0xFF;
    encrypted_data[8] = (instance->stored_part2_high >> 8) & 0xFF;
    encrypted_data[9] = instance->stored_part2_high & 0xFF;

    encrypted_data[10] = (instance->stored_part2_low >> 24) & 0xFF;
    encrypted_data[11] = (instance->stored_part2_low >> 16) & 0xFF;
    encrypted_data[12] = (instance->stored_part2_low >> 8) & 0xFF;
    encrypted_data[13] = instance->stored_part2_low & 0xFF;

    encrypted_data[14] = (instance->data_part3 >> 8) & 0xFF;
    encrypted_data[15] = instance->data_part3 & 0xFF;

    uint8_t fx_byte0 = (instance->stored_part1_high >> 24) & 0xFF;
    uint8_t fx_byte1 = (instance->stored_part1_high >> 16) & 0xFF;
    instance->fx_field = ((fx_byte0 & 0xF) << 4) | (fx_byte1 & 0xF);

    uint8_t aes_key[16];
    get_kia_v6_aes_key(aes_key);
    uint8_t expanded_key[176];
    aes_key_expansion(aes_key, expanded_key);
    aes128_decrypt(expanded_key, encrypted_data);

    uint8_t* decrypted = encrypted_data;

    uint8_t calculated_crc = kia_v6_crc8(decrypted, 15);
    uint8_t stored_crc = decrypted[15];

    instance->generic.serial = ((uint32_t)decrypted[4] << 16) | ((uint32_t)decrypted[5] << 8) |
                               decrypted[6];

    instance->generic.btn = decrypted[7];

    instance->generic.cnt = ((uint32_t)decrypted[8] << 24) | ((uint32_t)decrypted[9] << 16) |
                            ((uint32_t)decrypted[10] << 8) | decrypted[11];

    instance->crc1_field = decrypted[12];

    instance->crc2_field = decrypted[15];

    return (calculated_crc ^ stored_crc) < 2;
}

static void kia_v6_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderKiaV6* instance = input->decoder;


    kia_v6_decrypt(instance);

    uint32_t key1_hi = instance->stored_part1_high;
    uint32_t key1_lo = instance->stored_part1_low;
    uint32_t key2_hi = instance->stored_part2_high;
    uint32_t key2_lo = instance->stored_part2_low;

    uint32_t key2_uVar4 = key2_hi << 16;
    uint32_t key2_uVar2 = key2_lo >> 16;
    uint32_t key2_uVar1 = key2_hi >> 16;
    uint32_t key2_combined = key2_uVar4 | key2_uVar2;

    uint32_t key2_uVar3 = key2_lo << 16;
    uint32_t key2_second = (instance->data_part3 & 0xFFFF) | key2_uVar3;

    uint32_t serial_6 = instance->generic.serial & 0xFFFFFF;

    subghz_custom_btn_set_max(4);
    uint8_t display_btn = (uint8_t)(instance->generic.btn & 0x0FU);
    switch(subghz_custom_btn_get()) {
    case SUBGHZ_CUSTOM_BTN_UP:
        display_btn = 0x01U;
        break;
    case SUBGHZ_CUSTOM_BTN_DOWN:
        display_btn = 0x02U;
        break;
    case SUBGHZ_CUSTOM_BTN_LEFT:
        display_btn = 0x03U;
        break;
    case SUBGHZ_CUSTOM_BTN_RIGHT:
        display_btn = 0x04U;
        break;
    case SUBGHZ_CUSTOM_BTN_OK:
    default:
        break;
    }

    const char* btn_name;
    switch(display_btn & 0x0F) {
    case 0x01:
        btn_name = "Lock";
        break;
    case 0x02:
        btn_name = "Unlock";
        break;
    case 0x03:
        btn_name = "Trunk";
        break;
    case 0x04:
        btn_name = "Panic";
        break;
    default:
        btn_name = "Unknown";
        break;
    }

    furi_string_printf(
        output,
        "%s %dbit\r\n"
        "%08lX%08lX%04lX\r\n"
        "%08lX%08lX Fx:%02X\r\n"
        "Ser:%06lX Btn:%01X[%s]\r\n"
        "Cnt:%08lX CRC:%02X-%02X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        key1_hi,
        key1_lo,
        key2_uVar1,
        key2_combined,
        key2_second,
        instance->fx_field,
        serial_6,
        display_btn,
        btn_name,
        instance->generic.cnt,
        instance->crc1_field,
        instance->crc2_field);
}

static const SubGhzDecodeEntry kia_v6_items[] = {{.name = "KIA/HYU V6", .get_string = kia_v6_decode}};
static const SubGhzDecodeRegistry kia_v6_registry = {.items = kia_v6_items, .size = 1};
static const FlipperAppPluginDescriptor decode_kia_v6_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &kia_v6_registry};
const FlipperAppPluginDescriptor* decode_kia_v6_ep(void) { return &decode_kia_v6_descriptor; }
