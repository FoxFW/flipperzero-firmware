#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Low-RAM "CLI soft lock" for the RAM-heavy Sub-GHz operations (live Read,
 * Read RAW capture, RAW Decode).
 *
 * A connected PC (qFlipper, or a CLI/RPC client) holds a large RPC/CLI session
 * resident (~15 KB), which can tip a decode/record into an OOM / MPU fault.
 * When free heap is below the threshold, this HARD-DISCONNECTS the USB device
 * (furi_hal_usb_set_config(NULL)) so the PC session is torn down and its RAM is
 * reclaimed -- the PC shows as disconnected and AUTO-RECONNECTS when the guard
 * is released. This is the garage app's "CLI soft lock"; it is the only path
 * that actually frees RAM (the screen-stream placeholder does not).
 *
 * It engages ONLY when heap is genuinely low, so a lightweight CLI session
 * (PuTTY, ~25 KB free) that does not need it stays connected. Unplugged users
 * never see any of this. */

/* Hard-disconnect threshold: engage the soft lock only below this free heap.
 * Kept safely below a lightweight CLI's free heap so PuTTY stays connected. */
#define SUBGHZ_LOWRAM_SOFTLOCK_THRESHOLD 20480u /* 20 KiB */

/* True when free heap is below the soft-lock threshold. */
bool subghz_lowram_should_softlock(void);

/* If free heap is below `threshold`, disconnect the PC to reclaim its RAM and
 * wait for the teardown to settle. Returns true if the lock is now engaged
 * (or was already), false if no lock was needed. */
bool subghz_lowram_mitigate(size_t threshold);

/* Disconnect the PC (idempotent). */
void subghz_lowram_softlock_engage(void);

/* Reconnect the PC if this guard disconnected it (idempotent). */
void subghz_lowram_softlock_release(void);

#ifdef __cplusplus
}
#endif
