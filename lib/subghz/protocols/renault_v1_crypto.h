#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Crypto + key-recovery for the RenNissanVAZ / A5 protocol, ported faithfully from the bought
 * Largus.cpp (Hitag2-style stream cipher). Host-validated against all six fobs: fobs 1-4 recover a
 * UNIQUE plaintext IV (exactly one match in the 2^18 range), fobs 5-6 exhaust the range with none --
 * the same solve/fail pattern the Pandora shows, which is why 5/6 are not recoverable from RF alone.
 *
 * The brute is a single 2^18 range and takes seconds on the M4; it MUST run on a worker thread, never
 * in feed(). This mirrors the PSA bruteforce engine so it can reuse the exact same scene, progress
 * view and worker plumbing (see applications/main/subghz/scenes/subghz_scene_bruteforce.c). */

/* Size of the single IV brute range = 2^18. Shared by the search loop, the progress bounds and the
 * GUI total so the progress bar can never mis-scale against the actual search. */
#define RENAULT_V1_BF_RANGE 0x40000

typedef enum {
    RenaultV1BfStateIdle = 0,
    RenaultV1BfStateRunning,
    RenaultV1BfStateFound,
    RenaultV1BfStateNotFound,
    RenaultV1BfStateCancelled,
} RenaultV1BfState;

typedef struct {
    /* Written by the worker, polled by the GUI tick -- volatile like the PSA context. */
    volatile bool cancel_requested;
    volatile uint32_t progress; // rounds tried so far
    volatile uint8_t state; // RenaultV1BfState
    uint32_t total; // RENAULT_V1_BF_RANGE

    /* Input, filled by init from the decoder. */
    uint8_t payload[11];

    /* Result, filled by run on success: the 32-bit decrypted plaintext IV. This alone is the secret a
     * rolling-code TX re-encrypts, and every displayed field (button, counter, fixed part) is a slice
     * of it -- so the brute `round` (a per-fob constant, not the counter) is not returned. */
    uint8_t iv[4];
} RenaultV1BfContext;

/* Run the 2^18 IV brute. Blocking -- call on the worker thread. Sets ctx->state to Found/NotFound/
 * Cancelled and, on Found, fills ctx->iv (the decrypted plaintext IV). */
void subghz_protocol_renault_v1_bf_run(RenaultV1BfContext* ctx);

/* NOTE: the decoder-touching hooks needs_bruteforce / bf_init / bf_apply are declared in renault_v1.h,
 * next to the file-local decoder struct they are implemented against; this header is the cipher only. */

/* Forward encrypt: turn a plaintext IV (+ serial) into the 11-byte on-air payload -- the exact inverse
 * of recovery, so encrypt(serial, recovered_iv) reproduces the captured payload. The caller sets the
 * desired 16-bit counter and one-hot button into iv_plain first (button = iv0 low nibble); there is no
 * implicit increment. This is what lets a re-encoding TX (rolling Send, Set Counter, Counter-BF) build a
 * valid fresh frame from the stored Seed instead of replaying the captured one. */
void subghz_protocol_renault_v1_encrypt(
    const uint8_t serial[4],
    const uint8_t iv_plain[4],
    uint8_t out[11]);
