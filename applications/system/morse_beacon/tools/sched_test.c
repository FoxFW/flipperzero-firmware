/* Host check of the interval scheduling lifted verbatim from beacon.c:
 * the deadline arithmetic, the slot skipping and the wraparound test. */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

static inline bool tick_reached(uint32_t now, uint32_t target) {
    return (int32_t)(now - target) >= 0;
}

/* Returns the transmission start times over `horizon` ms. */
static int run(uint32_t start_tick, uint32_t period_ms, uint32_t tx_ms,
               bool anchor_start, uint32_t horizon, uint32_t* out, int max,
               int* skipped) {
    uint32_t period_ticks = period_ms;           /* tick_hz == 1000 on the F7 */
    uint32_t next_id_tick = start_tick + period_ticks;
    uint32_t now = start_tick;
    int n = 0;
    *skipped = 0;

    while((uint32_t)(now - start_tick) < horizon && n < max) {
        if(tick_reached(now, next_id_tick)) {
            out[n++] = now;
            now += tx_ms;                        /* the transmission */
            if(anchor_start) {
                next_id_tick += period_ticks;
                while(tick_reached(now, next_id_tick)) { next_id_tick += period_ticks; (*skipped)++; }
            } else {
                next_id_tick = now + period_ticks;
            }
        }
        now += 20;                               /* BEACON_TICK_MS */
    }
    return n;
}

static void report(const char* name, uint32_t start, uint32_t period, uint32_t tx,
                   bool anchor, uint32_t horizon) {
    uint32_t t[32];
    int skipped = 0;
    int n = run(start, period, tx, anchor, horizon, t, 32, &skipped);
    printf("%-34s starts:", name);
    for(int i = 0; i < n; i++) printf(" %.2f", (t[i] - start) / 1000.0);
    printf("\n%-34s gaps:  ", "");
    for(int i = 1; i < n; i++) printf(" %.2f", (t[i] - t[i-1]) / 1000.0);
    if(skipped) printf("   (%d slot(s) skipped)", skipped);
    printf("\n");
}

int main(void) {
    const uint32_t TX = 6830;  /* "DE CALLSIGN" 18 wpm + preamble + tail */

    report("15 s, start-to-start", 0, 15000, TX, true, 70000);
    report("15 s, end-to-start", 0, 15000, TX, false, 70000);
    report("60 s, start-to-start", 0, 60000, TX, true, 250000);
    report("5 s, start, TX overruns", 0, 5000, TX, true, 45000);

    /* The rollover seam: start just under 2^32 so the deadline wraps. */
    report("15 s, start, across wrap", 0xFFFFF000u, 15000, TX, true, 70000);
    return 0;
}
