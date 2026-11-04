#pragma once

/* Nice O-Code shared crypto + constants. Included by the detector (nice_flor_s.c) and by both
 * split plugins (decode_nice_flor_s.fal / enc_nice_flor_s.fal) so the cipher is defined once and
 * stays byte-identical across all three. Pure computation on a caller-supplied 32-byte rainbow
 * table buffer - no I/O, no persistent state. Host-verified in ocode_crypto_test.c (cnt_swap
 * involution; encrypt_ic<->decrypt_ic; make_key->nice_o_decrypt over 300k; cert_data). The
 * including TU must already provide subghz_protocol_blocks_get_bit_array() from the blocks headers. */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define NICE_ONE_COUNT_BIT        72
#define NICE_O_NAME               "Nice O-Code"
#define NICE_O_LONG_COUNT_BIT     112
#define NICE_FLOR_S_IC_PLAIN      0xFFFFu
#define NICE_O_FRESH_REPORTS      1u
#define NICE_O_MIN_SAMPLES        4
#define NICE_O_MAX_SAMPLES        16
#define NICE_FLOR_S_CREATE_PARCEL 0x3
#define NICE_O_CERT_SENDS         20
#define NICE_O_CERT_PARCELS       10
#define NICE_O_RELEASE_PARCELS    2

/* An O-Code remote's k departs from the Nice One formula at parcels 8..B. */
static const uint8_t nice_o_k[16] =
    {0x8F, 0x8E, 0x8D, 0x8C, 0x84, 0x85, 0x86, 0x87, 0x8F, 0x8C, 0x89, 0x8D, 0x8C, 0x8D, 0x8E, 0x8F};

/* O-Code counter: base-8 odometer with octal digits swapped. Involution (same call both ways).
 * Applied only at the cipher, so generic.cnt / file / editor all hold the plain count. */
static inline uint16_t nice_o_cnt_swap(uint16_t cnt) {
    uint16_t b = cnt & 0x2492;
    return cnt ^ (uint16_t)((b << 1) | ((b ^ 0x2492) >> 1));
}

static inline uint32_t nice_o_one_crc(uint8_t* p, uint8_t start, uint8_t end) {
    uint8_t crc = 0;
    uint8_t crc_data = 0xff;
    for(uint8_t i = start; i < end; i++) {
        if(subghz_protocol_blocks_get_bit_array(p, i)) {
            crc = crc_data ^ 1;
        } else {
            crc = crc_data;
        }
        crc_data >>= 1;
        if(crc & 0x01) crc_data ^= 0x97;
    }
    crc = 0;
    for(uint8_t i = 0; i < 8; i++) {
        crc <<= 1;
        if((crc_data >> i) & 0x01) crc |= 1;
    }
    return crc;
}

static inline void nice_o_magic_xor(uint8_t* p, uint8_t k) {
    for(uint8_t i = 1; i < 6; i++) p[i] ^= k;
}
static inline uint8_t nice_o_tbl(const uint8_t* buf, uint8_t address) {
    return buf[address];
}

/* flags: bit0 release marker, bit1 certificate frame, bit2 O-Code table (else Nice One formula). */
static inline void nice_o_one_get_data(uint8_t* p, uint8_t num_parcel, uint8_t flags) {
    uint8_t k = 0;
    num_parcel &= 0x0F;
    p[1] = (p[1] & 0x0f) | ((0x0f ^ (p[0] & 0x0F) ^ num_parcel) << 4);
    if(flags & 0x4) {
        k = nice_o_k[num_parcel];
    } else {
        k = num_parcel ^ (num_parcel < 4 ? 0x8f : 0x80);
    }
    if(flags & 0x2) k |= 0x40;
    p[7] = k;
    p[8] = (uint8_t)(((flags & 0x3) ^ k) << 4);
    uint8_t crc = (uint8_t)nice_o_one_crc(p, 4, 68);
    p[8] |= crc >> 4;
    p[9] = crc << 4;
}

/* The 40 bits a cert frame adds after the 72-bit one. p must hold 15 bytes. */
static inline void nice_o_cert_data(uint8_t* p, uint8_t index, uint32_t cert, uint64_t body) {
    const uint32_t v = (cert ^ (uint32_t)body) & 0xFFFFFF;
    p[9] |= 0x0E;
    p[10] = (uint8_t)(index << 4) | (uint8_t)(v >> 20);
    p[11] = (uint8_t)(v >> 12);
    p[12] = (uint8_t)(v >> 4);
    p[13] = (uint8_t)(v << 4);
    const uint8_t crc = (uint8_t)nice_o_one_crc(p, 68, 108);
    p[13] |= crc >> 4;
    p[14] = (uint8_t)(crc << 4);
}

/* IC-parameterized cipher. ic == 0xFFFF gives plain Nice Flor-S. Operate on a RAM buffer. */
static inline uint64_t nice_o_encrypt_buf(uint64_t data, uint16_t ic, const uint8_t* buf) {
    uint8_t* p = (uint8_t*)&data;
    uint8_t k = 0;
    for(uint8_t y = 0; y < 2; y++) {
        k = nice_o_tbl(buf, p[0] & 0x1f);
        nice_o_magic_xor(p, k);
        p[5] &= 0x0f;
        p[0] ^= k & 0xe0;
        k = nice_o_tbl(buf, p[0] >> 3) + 0x25;
        nice_o_magic_xor(p, k);
        p[5] &= 0x0f;
        p[0] ^= k & 0x7;
        if(y == 0) { k = p[0]; p[0] = p[1]; p[1] = k; }
    }
    p[5] = ~p[5] & 0x0f;
    k = ~p[4]; p[4] = p[0] ^ (uint8_t)(ic >> 8); p[0] = ~p[2]; p[2] = k;
    k = p[1] ^ (uint8_t)ic; p[1] = ~p[3]; p[3] = k;
    return data;
}

static inline uint64_t nice_o_decrypt_buf(uint64_t data, uint16_t ic, const uint8_t* buf) {
    uint8_t* p = (uint8_t*)&data;
    uint8_t k = 0;
    k = p[4] ^ (uint8_t)(ic >> 8); p[5] = ~p[5]; p[4] = ~p[2]; p[2] = ~p[0]; p[0] = k;
    k = p[3] ^ (uint8_t)ic; p[3] = ~p[1]; p[1] = k;
    for(uint8_t y = 0; y < 2; y++) {
        k = nice_o_tbl(buf, p[0] >> 3) + 0x25;
        nice_o_magic_xor(p, k);
        p[5] &= 0x0f;
        p[0] ^= k & 0x7;
        k = nice_o_tbl(buf, p[0] & 0x1f);
        nice_o_magic_xor(p, k);
        p[5] &= 0x0f;
        p[0] ^= k & 0xe0;
        if(y == 0) { k = p[0]; p[0] = p[1]; p[1] = k; }
    }
    return data;
}

/* Per-parcel obfuscation mask. n = parcel 0..15. */
static inline void nice_o_mask(uint8_t n, uint32_t* mask_32, uint16_t* mask_16) {
    const uint32_t a = (uint32_t)(0x0Eu - n) & 0x0Fu;
    const uint32_t b = (uint32_t)(n + 2u);
    const uint32_t odd = (uint32_t)n & 1u;
    const uint32_t c = b + (odd << 1);
    const uint32_t bn = b & 0x0Fu, cn = c & 0x0Fu;
    uint32_t m = (a << 8) | (bn << 4) | (bn << 20) | (a << 24) | cn | (cn << 16);
    m |= (((((b ^ (uint32_t)(0x0Eu - n) ^ c) & 0x0Fu) + (odd << 2)) << 12) & 0xFFFFu);
    if(mask_32) *mask_32 = m & 0x0FFFFFFFu;
    if(mask_16) *mask_16 = (uint16_t)(((m & 0x00FFu) << 8) | ((m >> 8) & 0x00FFu));
}

static inline uint8_t nice_o_get_parcel(uint64_t data) {
    return (uint8_t)((((data >> 48) & 0x0Fu) ^ ((data >> 44) & 0x0Fu)) ^ 0x0Fu);
}

/* Returns serial/cnt/btn from an O-Code frame against a RAM table. */
static inline void nice_o_decode_frame(
    uint64_t data, uint16_t ic, const uint8_t* buf,
    uint32_t* serial, uint16_t* cnt, uint8_t* btn) {
    uint32_t mask_32 = 0;
    uint16_t mask_16 = 0;
    nice_o_mask(nice_o_get_parcel(data), &mask_32, &mask_16);
    uint64_t decrypted = nice_o_decrypt_buf(data, (uint16_t)(ic ^ mask_16), buf);
    if(cnt) *cnt = nice_o_cnt_swap((uint16_t)decrypted);
    if(serial) *serial = (uint32_t)((decrypted >> 16) ^ mask_32) & 0x0FFFFFFFu;
    if(btn) *btn = (uint8_t)((data >> 48) & 0x0Fu);
}

/* Builds the 72-bit body for one parcel of an O-Code press. */
static inline uint64_t nice_o_make_key(
    uint32_t serial, uint16_t cnt, uint8_t btn, uint8_t parcel, uint16_t ic, const uint8_t* buf) {
    parcel &= 0x0Fu;
    uint32_t mask_32 = 0;
    uint16_t mask_16 = 0;
    nice_o_mask(parcel, &mask_32, &mask_16);
    const uint64_t plain =
        ((uint64_t)((serial ^ mask_32) & 0x0FFFFFFFu) << 16) | nice_o_cnt_swap(cnt);
    const uint64_t enc_part = nice_o_encrypt_buf(plain, (uint16_t)(ic ^ mask_16), buf);
    const uint8_t byte = (uint8_t)((btn & 0x0Fu) << 4 | (0x0Fu ^ (btn & 0x0Fu) ^ parcel));
    return (uint64_t)byte << 44 | enc_part;
}
