#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* FoxFW dual-radio timestamped edge capture (Phase 1 -- the shared DWT timebase).
 *
 * Both Sub-GHz radios capture edges as deltas only, on independent timers (TIM2
 * internal, TIM17 external), so their streams cannot be aligned from the deltas
 * alone. When fusion is ARMED, each radio's capture ISR stamps every edge with
 * DWT->CYCCNT -- one monotonic 64 MHz core cycle counter shared by both ISRs --
 * and pushes (abs_ts, level) into a per-radio ring. A consumer thread drains
 * both rings and (later, Phase 2) merges them on that one clock.
 *
 * When NOT armed the capture path is byte-for-byte unchanged: the ISRs read a
 * single bool and skip, so there is zero overhead with fusion off. */

/* Packed edge: DWT cycle timestamp with the edge level in bit 0. DWT @ 64 MHz =
 * 15.6 ns per LSB, so borrowing bit 0 for the level costs one LSB of timing
 * resolution -- negligible against Sub-GHz bit periods of 100s of microseconds. */
typedef uint32_t FoxFusionEdge;
#define FOX_FUSION_EDGE_TS(e)    ((uint32_t)((e) & ~1u))
#define FOX_FUSION_EDGE_LEVEL(e) ((bool)((e) & 1u))

/* Read directly by the two capture ISRs: `if(fox_fusion_armed) fox_fusion_push_*`
 * so the off path is a single load + untaken branch. */
extern volatile bool fox_fusion_armed;

/* Allocate both rings, ensure DWT CYCCNT is running, then arm. Returns false (and
 * stays disarmed) if the rings cannot be allocated. Call before starting RX. */
bool fox_fusion_arm(void);

/* Disarm (ISRs stop pushing). Rings are kept so a final drain can still run.
 * Call after both radios' async RX are stopped, before fox_fusion_free(). */
void fox_fusion_disarm(void);

/* Free both rings. Only safe once RX is stopped and fusion is disarmed. */
void fox_fusion_free(void);

/* Called from the capture ISRs only (firmware-internal; not exported). Each reads
 * DWT->CYCCNT itself so both streams land on the same clock read path. */
void fox_fusion_push_int(bool level);
void fox_fusion_push_ext(bool level);

/* Drain up to `max` edges (oldest first) into `out`. Returns the count copied. */
size_t fox_fusion_drain_int(FoxFusionEdge* out, size_t max);
size_t fox_fusion_drain_ext(FoxFusionEdge* out, size_t max);

/* Diagnostics: edges dropped on a full ring, and current ring occupancy. */
uint32_t fox_fusion_overflow_int(void);
uint32_t fox_fusion_overflow_ext(void);
size_t fox_fusion_count_int(void);
size_t fox_fusion_count_ext(void);

#ifdef __cplusplus
}
#endif
