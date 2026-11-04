#include "keeloq_common.h"

#include <furi.h>

#include <m-array.h>

#define bit(x, n) (((x) >> (n)) & 1)
#define g5(x, a, b, c, d, e) \
    (bit(x, a) + bit(x, b) * 2 + bit(x, c) * 4 + bit(x, d) * 8 + bit(x, e) * 16)

inline uint32_t subghz_protocol_keeloq_common_encrypt(const uint32_t data, const uint64_t key) {
    uint32_t x = data, r;
    for(r = 0; r < 528; r++)
        x = (x >> 1) ^ ((bit(x, 0) ^ bit(x, 16) ^ (uint32_t)bit(key, r & 63) ^
                         bit(KEELOQ_NLF, g5(x, 1, 9, 20, 26, 31)))
                        << 31);
    return x;
}

inline uint32_t subghz_protocol_keeloq_common_decrypt(const uint32_t data, const uint64_t key) {
    uint32_t x = data, r;
    for(r = 0; r < 528; r++)
        x = (x << 1) ^ bit(x, 31) ^ bit(x, 15) ^ (uint32_t)bit(key, (15 - r) & 63) ^
            bit(KEELOQ_NLF, g5(x, 0, 8, 19, 25, 30));
    return x;
}

inline uint64_t subghz_protocol_keeloq_common_normal_learning(uint32_t data, const uint64_t key) {
    uint32_t k1, k2;

    data &= 0x0FFFFFFF;
    data |= 0x20000000;
    k1 = subghz_protocol_keeloq_common_decrypt(data, key);

    data &= 0x0FFFFFFF;
    data |= 0x60000000;
    k2 = subghz_protocol_keeloq_common_decrypt(data, key);

    return ((uint64_t)k2 << 32) | k1;
}

inline uint64_t subghz_protocol_keeloq_common_secure_learning(
    uint32_t data,
    uint32_t seed,
    const uint64_t key) {
    uint32_t k1, k2;

    data &= 0x0FFFFFFF;
    k1 = subghz_protocol_keeloq_common_decrypt(data, key);
    k2 = subghz_protocol_keeloq_common_decrypt(seed, key);

    return ((uint64_t)k1 << 32) | k2;
}

inline uint64_t
    subghz_protocol_keeloq_common_magic_xor_type1_learning(uint32_t data, uint64_t xor) {
    data &= 0x0FFFFFFF;
    return (((uint64_t)data << 32) | data) ^ xor;
}

inline uint64_t
    subghz_protocol_keeloq_common_faac_learning(const uint32_t seed, const uint64_t key) {
    uint16_t hs = seed >> 16;
    const uint16_t ending = 0x544D;
    uint32_t lsb = (uint32_t)hs << 16 | ending;
    uint64_t man = (uint64_t)subghz_protocol_keeloq_common_encrypt(seed, key) << 32 |
                   subghz_protocol_keeloq_common_encrypt(lsb, key);
    return man;
}

inline uint64_t
    subghz_protocol_keeloq_common_magic_serial_type1_learning(uint32_t data, uint64_t man) {
    return (man & 0xFFFFFFFF) | ((uint64_t)data << 40) |
           ((uint64_t)(((data & 0xff) + ((data >> 8) & 0xFF)) & 0xFF) << 32);
}

inline uint64_t
    subghz_protocol_keeloq_common_magic_serial_type2_learning(uint32_t data, uint64_t man) {
    uint8_t* p = (uint8_t*)&data;
    uint8_t* m = (uint8_t*)&man;
    m[7] = p[0];
    m[6] = p[1];
    m[5] = p[2];
    m[4] = p[3];
    return man;
}

inline uint64_t
    subghz_protocol_keeloq_common_magic_serial_type3_learning(uint32_t data, uint64_t man) {
    return (man & 0xFFFFFFFFFF000000) | (data & 0xFFFFFF);
}


/* ---- AERF (type 14) manufacturer derivation (JCM-family manufacturer_nl_extend) ----
 * Explicit shift guards (n>=32 -> 0) match the ARM target AND KeeLoq-RX kl_crack.py
 * (NOT x86 mask-mod-32), so host and device agree. See
 * claude/KEELOQ_AERF_JCM_DECODER_REVIEW.md for why the raw decompiled shifts were unsafe. */
static uint32_t keeloq_common_nl_extend(uint32_t x, uint32_t k_lo, uint32_t k_hi, uint32_t outer_limit) {
    uint32_t r5 = 0u;
    while(r5 != outer_limit) {
        if(r5 < 0x210u) {
            uint32_t r7 = ((x >> 15) & 1u) ^ ((x >> 1) | (x << 31));
            uint32_t sh = (15u - r5) & 0x3Fu;
            uint32_t lr = ((32u - sh) >= 32u) ? 0u : (k_hi << (32u - sh));
            uint32_t klo = (sh >= 32u) ? 0u : (k_lo >> sh);
            uint32_t ip = (klo < 32u) ? (((sh - 32u) >= 32u) ? 0u : (k_hi >> (sh - 32u))) : 0u;
            uint32_t r1 = ((klo | lr | ip) & 1u) ^ r7;
            uint32_t t = ((x >> 25) & 1u) + (((x >> 30) & 1u) << 1);
            t = ((x >> 19) & 1u) + (t << 1);
            t = ((x >> 8) & 1u) + (t << 1);
            t = (x & 1u) + (t << 1);
            x = ((KEELOQ_NLF >> (t & 31u)) & 1u) ^ r1;
        }
        r5 += 1u;
    }
    return x;
}

uint64_t subghz_protocol_keeloq_common_learning_aerf(uint32_t fix, const uint64_t key) {
    uint32_t k_lo = (uint32_t)key;
    uint32_t k_hi = (uint32_t)(key >> 32);
    uint32_t d = fix & 0x0FFFFFFFu;
    uint32_t k1 = keeloq_common_nl_extend(d | 0x20000000u, k_lo, k_hi, 0x40u);
    uint32_t k2 = keeloq_common_nl_extend(d | 0x60000000u, k_lo, k_hi, 0x40u);
    return ((uint64_t)k2 << 32) | k1;
}

uint32_t subghz_protocol_keeloq_common_decrypt_aerf(uint32_t hop, uint64_t man, uint32_t outer_limit) {
    return keeloq_common_nl_extend(hop, (uint32_t)man, (uint32_t)(man >> 32), outer_limit);
}
