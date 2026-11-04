#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "hitag2_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HITAG2_HELL_MAX_CANDIDATES 128U

typedef struct {

    bool (*progress_cb)(uint8_t pct, uint64_t states_tested, void* ctx);
    void* progress_ctx;

    uint32_t timeout_ms;

    uint32_t (*now_ms_cb)(void);

    uint32_t l0_start;
    uint32_t l0_end;

    /* Optional: checked cheaply inside the hot i1 loop so a running sweep aborts within one
     * inner iteration (~ms) instead of only at the far-apart progress callbacks. */
    const volatile bool* cancel;
} Hitag2HellConfig;

typedef struct {
    Hitag2State candidates[HITAG2_HELL_MAX_CANDIDATES];
    uint32_t candidate_count;
    uint64_t states_tested_total;
    bool cancelled;
    bool timed_out;
    bool overflow;
} Hitag2HellResult;

bool hitag2_hell_recover(
    uint32_t authenticator,
    const Hitag2HellConfig* config,
    Hitag2HellResult* result);

bool hitag2_hell_self_test(void);

#ifdef __cplusplus
}
#endif
