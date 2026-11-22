#pragma once

#include <stddef.h>
#include <stdint.h>

#define SUBGHZ_BITBUF_BYTES(n) (((n) + 7U) / 8U)

static inline uint8_t subghz_bitbuf_get(const uint8_t* buf, size_t i) {
    return (uint8_t)((buf[i >> 3] >> (i & 7U)) & 1U);
}

static inline void subghz_bitbuf_set(uint8_t* buf, size_t i, uint32_t v) {
    if(v & 1U) {
        buf[i >> 3] |= (uint8_t)(1U << (i & 7U));
    } else {
        buf[i >> 3] &= (uint8_t)~(1U << (i & 7U));
    }
}

static inline void subghz_bitbuf_shift_down1(uint8_t* buf, size_t n) {
    if(n < 2) return;
    uint8_t last = subghz_bitbuf_get(buf, n - 1);
    size_t bytes = SUBGHZ_BITBUF_BYTES(n);
    for(size_t k = 0; k < bytes; k++) {
        uint8_t next = (k + 1 < bytes) ? buf[k + 1] : 0;
        buf[k] = (uint8_t)((buf[k] >> 1) | (uint8_t)(next << 7));
    }
    subghz_bitbuf_set(buf, n - 1, last);
}
