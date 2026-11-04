#include "renault_v1_crypto.h"
#include <furi.h>

/* Hitag2-style cipher, ported line-for-line from the bought Largus.cpp and host-validated (see
 * temp/a5_crypto/GOLDEN_VECTORS.md). The working state is module-local rather than a struct so the
 * port stays byte-identical to the source. BOTH entry points -- the recovery brute (recover_iv, on the
 * bruteforce worker thread) and the forward encrypt (on the transmit path) -- mutate these same statics
 * with no lock. That is safe only because they never run at once: the bf worker is joined before any TX,
 * and the two live in different scenes, so at most one uses this state at a time. Do not call either
 * from a second thread without adding a guard. */

static unsigned char h_data[11]; // highest index touched is 10 (source declared 16; trimmed)
static unsigned char h_key[4];
static unsigned char h_iv[4];
static unsigned char h_key32[4];
static unsigned char h_key16[2];
static unsigned char hitag[13];

static void renault_v1_key_gen(const unsigned char* largus) {
    unsigned char t[6] = {0, 0, 0, 0, 0, 0};
    t[5] |=
        (((largus[3] & 0x40) >> 2) | ((largus[2] & 0x10) >> 3) | (((largus[1] & 0x08) >> 1) ^ 4) |
         (((largus[0] & 0x01) << 3) ^ 8) | ((largus[0] & 0x40) >> 2) |
         (((largus[1] & 0x04) << 3) ^ 0x20) | ((largus[3] & 0x80) >> 1) |
         ((largus[0] & 0x80) ^ 0x80));
    t[4] |=
        ((((largus[0] & 0x04) >> 2) ^ 1) | (((largus[3] & 0x04) >> 1) ^ 2) |
         (((largus[1] & 0x40) >> 4) ^ 4) | ((largus[3] & 0x02) << 2) | ((largus[0] & 0x01) << 4) |
         (((largus[0] & 0x02) << 4) ^ 0x20) | (((largus[2] & 0x01) << 6) ^ 0x40) |
         (((largus[1] & 0x10) << 3) ^ 0x80));
    t[3] |=
        ((((largus[0] & 0x10) >> 4) ^ 1) | ((largus[0] & 0x80) >> 6) |
         (((largus[2] & 0x80) >> 5) ^ 4) | ((largus[1] & 0x80) >> 4) |
         (((largus[0] & 0x40) >> 2) ^ 0x10) | (((largus[0] & 0x08) << 2) ^ 0x20) |
         ((largus[0] & 0x02) << 5) | ((largus[2] & 0x40) << 1));
    t[2] |= 0x80;
    t[2] |=
        (((largus[1] & 0x20) >> 5) | (((largus[1] & 0x20) >> 3) ^ 4) |
         (((largus[3] & 0x01) << 3) ^ 8) | (((largus[2] & 0x04) << 2) ^ 0x10) | (largus[0] & 0x20) |
         (((largus[2] & 0x08) << 3) ^ 0x40));
    t[1] |= 0x80;
    t[1] |=
        (((largus[0] & 0x04) >> 2) | ((largus[0] & 0x10) >> 3) | (((largus[3] & 0x10) >> 2) ^ 4) |
         (largus[1] & 0x10) | (((largus[1] & 0x02) << 4) ^ 0x20) |
         (((largus[3] & 0x08) << 3) ^ 0x40));
    t[0] |=
        ((((largus[2] & 0x20) >> 5) ^ 1) | ((largus[0] & 0x08) >> 2) |
         (((largus[0] & 0x20) >> 3) ^ 4) | ((largus[1] & 0x40) >> 3) |
         (((largus[1] & 0x80) >> 3) ^ 0x10) | ((largus[1] & 0x01) << 5) | ((largus[3] & 0x20) << 1) |
         (((largus[2] & 0x02) << 6) ^ 0x80));

    h_key32[0] = t[3];
    h_key32[1] = t[2];
    h_key32[2] = t[1];
    h_key32[3] = t[0];
    h_key16[0] = t[4];
    h_key16[1] = t[5];
}

static void renault_v1_shift_right(void) {
    int a, b;
    if(h_data[6] & 1) { a = 0x80; } else { a = 0; }
    h_data[6] /= 2;
    if(h_data[5] & 1) { b = 0x80; } else { b = 0; }
    h_data[5] = ((h_data[5] / 2) | a);
    if(h_data[4] & 1) { a = 0x80; } else { a = 0; }
    h_data[4] = ((h_data[4] / 2) | b);
    if(h_data[3] & 1) { b = 0x80; } else { b = 0; }
    h_data[3] = ((h_data[3] / 2) | a);
    if(h_data[2] & 1) { a = 0x80; } else { a = 0; }
    h_data[2] = ((h_data[2] / 2) | b);
    if(h_data[1] & 1) { b = 0x80; } else { b = 0; }
    h_data[1] = ((h_data[1] / 2) | a);
    if(h_data[6] & 1) { a = 0x80; } else { a = 0; }
    h_data[6] = ((h_data[6] / 2) | b);
    if(h_data[5] & 1) { b = 0x80; } else { b = 0; }
    h_data[5] = ((h_data[5] / 2) | a);
    if(h_data[4] & 1) { a = 0x80; } else { a = 0; }
    h_data[4] = ((h_data[4] / 2) | b);
    if(h_data[3] & 1) { b = 0x80; } else { b = 0; }
    h_data[3] = ((h_data[3] / 2) | a);
    if(h_data[2] & 1) { a = 0x80; } else { a = 0; }
    h_data[2] = ((h_data[2] / 2) | b);
    h_data[1] = ((h_data[1] / 2) | a);
}

/* = Largus.cpp bit_counter: one-hot 2^(byte&7). The source writes it as a doubling loop; this is the
 * identical result as a single shift, kept as a shift because bit_counter sits on the cipher's hottest
 * path (many calls per round across the whole 2^18 brute). Host-validated identical (88/88). */
static int renault_v1_bit_counter(char byte) {
    return 1 << (byte & 7);
}

static unsigned int renault_v1_h_out(void) {
    int a, b, c, d;
    c = 0;
    if(h_data[10] & 0x20) { a = 1; } else { a = 0; }
    if(h_data[10] & 0x10) { a |= 2; }
    if(h_data[10] & 4) { a |= 4; }
    if(h_data[10] & 2) {
        b = (renault_v1_bit_counter(a) & 0x2C);
    } else {
        b = (renault_v1_bit_counter(a) & 0x79);
    }
    if(b) { c = 1; }

    if(h_data[9] & 0x80) { a = 1; } else { a = 0; }
    if(h_data[9] & 8) { a |= 2; }
    if(h_data[9] & 2) { a |= 4; }
    if(h_data[9] & 1) {
        b = (renault_v1_bit_counter(a) & 0x66);
    } else {
        b = (renault_v1_bit_counter(a) & 0x71);
    }
    if(b) { c |= 2; }

    if(h_data[8] & 0x40) { a = 1; } else { a = 0; }
    if(h_data[8] & 4) { a |= 2; }
    if(h_data[8] & 1) { a |= 4; }
    if(h_data[7] & 0x20) {
        b = (renault_v1_bit_counter(a) & 0x66);
    } else {
        b = (renault_v1_bit_counter(a) & 0x71);
    }
    if(b) { c |= 4; }

    if(h_data[7] & 8) { a = 1; } else { a = 0; }
    if(h_data[7] & 4) { a |= 2; }
    if(h_data[7] & 1) { a |= 4; }
    if(h_data[6] & 0x40) {
        b = (renault_v1_bit_counter(a) & 0x66);
    } else {
        b = (renault_v1_bit_counter(a) & 0x71);
    }
    if(b) { c |= 8; }

    if(h_data[6] & 0x20) { a = 1; } else { a = 0; }
    if(h_data[5] & 0x10) { a |= 2; }
    if(h_data[5] & 8) { a |= 4; }
    if(h_data[5] & 2) {
        b = (renault_v1_bit_counter(a) & 0x2C);
    } else {
        b = (renault_v1_bit_counter(a) & 0x79);
    }
    if(b) {
        d = 0x07;
        if(c & 8) { d = 0x79; }
    } else {
        d = 0x7B;
        if(c & 8) { d = 0x28; }
    }
    return (renault_v1_bit_counter(c) & d);
}

static int renault_v1_shift_h_data(int a) {
    h_data[10] = ((h_data[10] * 2) | (h_data[9] / 0x80));
    h_data[9] = ((h_data[9] * 2) | (h_data[8] / 0x80));
    h_data[8] = ((h_data[8] * 2) | (h_data[7] / 0x80));
    h_data[7] = ((h_data[7] * 2) | (h_data[6] / 0x80));
    h_data[6] = ((h_data[6] * 2) | (h_data[5] / 0x80));
    h_data[5] *= 2;
    if((a & 1)) { h_data[5] = (h_data[5] | 1); }
    return h_data[5];
}

static void renault_v1_shift_iv(void) {
    h_iv[3] = ((h_iv[3] * 2) | (h_iv[2] / 0x80));
    h_iv[2] = ((h_iv[2] * 2) | (h_iv[1] / 0x80));
    h_iv[1] = ((h_iv[1] * 2) | (h_iv[0] / 0x80));
    h_iv[0] *= 2;
}

static void renault_v1_shift_key(void) {
    h_key[3] = ((h_key[3] * 2) | (h_key[2] / 0x80));
    h_key[2] = ((h_key[2] * 2) | (h_key[1] / 0x80));
    h_key[1] = ((h_key[1] * 2) | (h_key[0] / 0x80));
    h_key[0] *= 2;
}

/* The two 32-round hitag_init loops from Largus.cpp, shared by the recovery test (result=0) and the
 * forward encrypt (result=1). Runs on whatever h_data/h_key/h_iv the caller seeded and mutates all
 * three in place; the caller reads h_key (keystream) / h_iv (decrypted) afterwards. */
static void renault_v1_hitag_rounds(void) {
    unsigned char a, b, c, d, e;
    for(a = 0; a < 32; ++a) {
        if(renault_v1_h_out()) { b = 1; } else { b = 0; }
        if(h_key[3] & 0x80) { b ^= 1; }
        if(h_iv[3] & 0x80) { b ^= 5; }
        renault_v1_shift_h_data(b);
        renault_v1_shift_key();
        renault_v1_shift_iv();
        if(b & 4) { h_iv[0] |= 1; }
    }
    for(a = 0; a < 32; ++a) {
        renault_v1_shift_key();
        if(renault_v1_h_out()) { h_key[0] |= 1; }
        c = ((h_data[10] & 0xB3) ^ (h_data[9] & 0x80) ^ (h_data[8] & 0x83) ^ (h_data[7] & 0x22) ^
             (h_data[5] & 0x73));
        if(c & 8) {
            d = (renault_v1_bit_counter(c) & 0x69);
        } else {
            d = (renault_v1_bit_counter(c) & 0x96);
        }
        c >>= 4;
        if(c & 8) {
            e = (renault_v1_bit_counter((c)) & 0x69);
        } else {
            e = (renault_v1_bit_counter((c)) & 0x96);
        }
        if(d) { b = 1; } else { b = 0; }
        if(e) { b ^= 3; }
        renault_v1_shift_h_data(b);
    }
}

/* Left-shift of the 48-bit h_data[1..6] -- the inverse of renault_v1_shift_right done during recovery
 * setup, used by the encrypt to turn the fresh keystream back into on-air byte order (Largus shift_left). */
static void renault_v1_shift_left(void) {
    int a, b;
    if(h_data[1] & 0x80) { a = 1; } else { a = 0; }
    h_data[1] *= 2;
    if(h_data[2] & 0x80) { b = 1; } else { b = 0; }
    h_data[2] = ((h_data[2] * 2) | a);
    if(h_data[3] & 0x80) { a = 1; } else { a = 0; }
    h_data[3] = ((h_data[3] * 2) | b);
    if(h_data[4] & 0x80) { b = 1; } else { b = 0; }
    h_data[4] = ((h_data[4] * 2) | a);
    if(h_data[5] & 0x80) { a = 1; } else { a = 0; }
    h_data[5] = ((h_data[5] * 2) | b);
    if(h_data[6] & 0x80) { b = 1; } else { b = 0; }
    h_data[6] = ((h_data[6] * 2) | a);
    if(h_data[1] & 0x80) { a = 1; } else { a = 0; }
    h_data[1] = ((h_data[1] * 2) | b);
    if(h_data[2] & 0x80) { b = 1; } else { b = 0; }
    h_data[2] = ((h_data[2] * 2) | a);
    if(h_data[3] & 0x80) { a = 1; } else { a = 0; }
    h_data[3] = ((h_data[3] * 2) | b);
    if(h_data[4] & 0x80) { b = 1; } else { b = 0; }
    h_data[4] = ((h_data[4] * 2) | a);
    if(h_data[5] & 0x80) { a = 1; } else { a = 0; }
    h_data[5] = ((h_data[5] * 2) | b);
    h_data[6] = ((h_data[6] * 2) | a);
}

/* Returns 1 when this IV candidate decrypts to a self-consistent key (the success test from the
 * source's hitag_init with result=0). */
static unsigned char renault_v1_hitag_try(void) {
    renault_v1_hitag_rounds();
    return ((h_key[3] == h_data[4]) && (h_key[2] == h_data[3]) && (h_key[1] == h_data[2]) &&
            (h_key[0] == h_data[1])) ?
               1 :
               0;
}

/* Core recovery: brute the 2^18 IV range for ctx->payload; on success fills ctx->iv (the 32-bit
 * decrypted plaintext IV). Polls ctx->cancel_requested and updates ctx->progress so the GUI can show a
 * bar. Returns true if an IV was found, false if the range was exhausted (fobs 5/6) or cancelled. */
static bool renault_v1_recover_iv(RenaultV1BfContext* ctx) {
    int b;
    uint32_t round;
    const unsigned char* data = ctx->payload;

    renault_v1_key_gen(data);

    for(b = 0; b < 11; ++b) h_data[b] = data[10 - b];
    renault_v1_shift_right();
    for(b = 0; b < 11; ++b) hitag[b] = h_data[b];
    hitag[11] = (hitag[6] & 0x03);
    hitag[12] = (data[4] & 0xF0);
    hitag[11] = (hitag[5] >> 4) | (hitag[11] << 4);
    hitag[12] = (hitag[5] << 4) | (hitag[12] >> 4);

    for(round = 0; round < RENAULT_V1_BF_RANGE; round += 1) {
        if((round & 0x3FF) == 0) {
            ctx->progress = round;
            if(ctx->cancel_requested) return false;
        }
        /* Seed the cipher state fresh for every round -- one copy here covers round 0 too, which the
         * source omitted (it ran round 0 against whatever state a prior recovery left: a ~2^-32
         * stale-state false-match and a blind spot for a genuine counter of 0). */
        h_data[5] = h_key16[1];
        h_data[6] = h_key16[0];
        h_data[7] = hitag[7];
        h_data[8] = hitag[8];
        h_data[9] = hitag[9];
        h_data[10] = hitag[10];
        h_key[3] = h_key32[3];
        h_key[2] = h_key32[2];
        h_key[1] = h_key32[1];
        h_key[0] = h_key32[0];
        h_iv[0] = hitag[12];
        h_iv[1] = (hitag[11] | round << 6);
        h_iv[2] = (round >> 2);
        h_iv[3] = (round >> 10);
        if(renault_v1_hitag_try()) {
            ctx->iv[0] = h_iv[0];
            ctx->iv[1] = h_iv[1];
            ctx->iv[2] = h_iv[2];
            ctx->iv[3] = h_iv[3];
            ctx->progress = RENAULT_V1_BF_RANGE;
            return true;
        }
    }
    /* Range exhausted (fobs 5/6): fill the bar so it reads 100% before the not-found message. Cancel
     * returns from inside the loop, so this point is exhaustion-only. */
    ctx->progress = RENAULT_V1_BF_RANGE;
    return false;
}

void subghz_protocol_renault_v1_bf_run(RenaultV1BfContext* ctx) {
    furi_assert(ctx);
    ctx->state = RenaultV1BfStateRunning;
    if(renault_v1_recover_iv(ctx)) {
        /* On success ctx->iv holds the 32-bit DECRYPTED IV. Every field the protocol needs -- button,
         * rolling counter, fixed part -- is a slice of that plaintext, so nothing else is returned: the
         * brute `round` is a per-fob constant (the fixed part), NOT the counter, and is discarded. */
        ctx->state = RenaultV1BfStateFound;
    } else {
        ctx->state = ctx->cancel_requested ? RenaultV1BfStateCancelled : RenaultV1BfStateNotFound;
    }
}

void subghz_protocol_renault_v1_encrypt(
    const uint8_t serial[4],
    const uint8_t iv_plain[4],
    uint8_t out[11]) {
    /* Forward path = Largus.cpp hitag_encripted + hitag_init(1), the exact inverse of recovery. The
     * caller passes the FINAL plaintext IV it wants on air (button in iv0 low nibble, 16-bit push_clock
     * in iv0_hi|iv1|iv2_lo, Fix in iv2_hi|iv3), so the source's implicit iv[0]+=0x10 is intentionally
     * NOT done here -- the counter is the caller's to set. The cleartext button (byte4) is taken from
     * iv0's low nibble, the SAME bits the cipher encrypts, so the two can never disagree. Chip_id + key
     * come from the serial alone (hitag[7..10] = serial[3..0], as recovery's setup leaves them). */
    const uint8_t button = iv_plain[0] & 0x0F;
    renault_v1_key_gen(serial);
    hitag[7] = serial[3];
    hitag[8] = serial[2];
    hitag[9] = serial[1];
    hitag[10] = serial[0];

    h_iv[0] = iv_plain[0];
    h_iv[1] = iv_plain[1];
    h_iv[2] = iv_plain[2];
    h_iv[3] = iv_plain[3];

    h_data[5] = h_key16[1];
    h_data[6] = h_key16[0];
    h_data[7] = hitag[7];
    h_data[8] = hitag[8];
    h_data[9] = hitag[9];
    h_data[10] = hitag[10];
    h_key[3] = h_key32[3];
    h_key[2] = h_key32[2];
    h_key[1] = h_key32[1];
    h_key[0] = h_key32[0];

    renault_v1_hitag_rounds();

    /* Keystream (h_key) -> on-air encrypted-IV bytes 6..9: shift_left undoes the setup's shift_right and
     * the (&0xFE)|2 restores the mangled bit, exactly as the source's result=1 tail. Host-checked to
     * reproduce bytes 6..9 byte-for-byte on all 88 captured frames. */
    h_data[1] = h_key[0];
    h_data[2] = h_key[1];
    h_data[3] = h_key[2];
    h_data[4] = h_key[3];
    renault_v1_shift_left();
    h_data[1] = ((h_data[1] & 0xFE) | 2);

    /* Cleartext counter+button in bytes 4,5 -- the source's h_data[5]/[6] formula is stubbed in
     * Largus.cpp (it overwrites h_data[5] with a clock expression masked &0xFE, discarding the low 2
     * bits), so it is NOT used. byte4 = button<<4 | counter[6..9]; byte5 top 6 bits = counter[0..5]<<2.
     * byte5 LOW 2 bits are LOAD-BEARING (they feed h_data[4] through the receiver's shift_right and gate
     * the brute) and equal the low 2 bits of h_data[5] straight out of shift_left above -- exactly the
     * bits the source threw away. Host-proven: with this the whole 11-byte frame reproduces every one of
     * the 88 captured frames byte-for-byte and re-recovers to the same IV (88/88). The 16-bit counter is
     * carved from the plaintext IV. */
    uint32_t cnt = ((uint32_t)(iv_plain[0] >> 4) & 0x0F) | ((uint32_t)iv_plain[1] << 4) |
                   (((uint32_t)iv_plain[2] & 0x0F) << 12);
    out[0] = serial[0];
    out[1] = serial[1];
    out[2] = serial[2];
    out[3] = serial[3];
    out[4] = (uint8_t)(((button & 0x0F) << 4) | ((cnt >> 6) & 0x0F));
    out[5] = (uint8_t)(((cnt << 2) & 0xFC) | (h_data[5] & 0x03));
    out[6] = h_data[4];
    out[7] = h_data[3];
    out[8] = h_data[2];
    out[9] = h_data[1];
    uint8_t crc = 0;
    for(int b = 0; b < 10; ++b) crc ^= out[b];
    out[10] = crc;
}

