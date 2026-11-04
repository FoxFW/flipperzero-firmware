#include "wrappers.h"

#include <furi/core/core_defines.h>

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#define STRTOD_MANTISSA_DIGITS_MAX 19

static const uint64_t strtod_pow10_bits[] = {
    0x4024000000000000ULL,
    0x4059000000000000ULL,
    0x40C3880000000000ULL,
    0x4197D78400000000ULL,
    0x4341C37937E08000ULL,
    0x4693B8B5B5056E17ULL,
    0x4D384F03E93FF9F5ULL,
    0x5A827748F9301D32ULL,
    0x75154FDD7F73BF3CULL,
};

#define STRTOD_EXP_MAX ((1 << COUNT_OF(strtod_pow10_bits)) - 1)

static double strtod_scale_pow10(double value, int32_t exp) {
    const bool negative_exp = exp < 0;
    if(negative_exp) exp = -exp;
    if(exp > STRTOD_EXP_MAX) exp = STRTOD_EXP_MAX;

    for(const uint64_t* bits = strtod_pow10_bits; exp; exp >>= 1, bits++) {
        if(!(exp & 1)) continue;

        double power;
        __builtin_memcpy(&power, bits, sizeof(power));
        value = negative_exp ? value / power : value * power;
    }

    return value;
}

static double strtod_parse(const char* in, char** tail) {
    const char* p = in;
    while(isspace((unsigned char)*p))
        p++;

    bool negative = false;
    if(*p == '+' || *p == '-') negative = (*p++ == '-');

    uint64_t mantissa = 0;
    int32_t exp10 = 0;
    uint8_t digits = 0;
    bool has_digits = false;

    bool fraction = false;
    for(;; p++) {
        if(*p == '.' && !fraction) {
            fraction = true;
            continue;
        }
        if(*p < '0' || *p > '9') break;
        has_digits = true;

        if(mantissa == 0 && *p == '0') {
            exp10 -= fraction;
        } else if(digits < STRTOD_MANTISSA_DIGITS_MAX) {
            mantissa = mantissa * 10 + (uint64_t)(*p - '0');
            digits++;
            exp10 -= fraction;
        } else {

            exp10 += !fraction;
        }
    }

    if(!has_digits) {

        if(tail) *tail = (char*)in;
        return 0;
    }

    if(*p == 'e' || *p == 'E') {
        const char* exp_start = p++;

        bool negative_exp = false;
        if(*p == '+' || *p == '-') negative_exp = (*p++ == '-');

        const char* exp_digits = p;
        int32_t exp = 0;
        for(; *p >= '0' && *p <= '9'; p++) {

            if(exp <= STRTOD_EXP_MAX) exp = exp * 10 + (*p - '0');
        }

        if(p == exp_digits) {

            p = exp_start;
        } else {
            exp10 += negative_exp ? -exp : exp;
        }
    }

    if(tail) *tail = (char*)p;

    const double value = strtod_scale_pow10((double)mantissa, exp10);

    uint64_t bits;
    __builtin_memcpy(&bits, &value, sizeof(bits));
    if(mantissa != 0 && (bits == 0 || bits >= 0x7FF0000000000000ULL)) errno = ERANGE;

    return negative ? -value : value;
}

float __wrap_strtof(const char* in, char** tail) {
    return (float)strtod_parse(in, tail);
}

double __wrap_strtod(const char* in, char** tail) {
    return strtod_parse(in, tail);
}
