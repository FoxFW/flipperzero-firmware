#include <fox_fusion.h>
#include <furi.h>
#include <stm32wbxx.h>

/* Per-radio SPSC ring (one producer = capture ISR, one consumer = drain thread).
 * Power-of-two size so the index wrap is a mask. Allocated on arm, freed after. */
#define FOX_FUSION_RING_SZ   2048u
#define FOX_FUSION_RING_MASK (FOX_FUSION_RING_SZ - 1u)

typedef struct {
    FoxFusionEdge     buf[FOX_FUSION_RING_SZ];
    volatile uint32_t head; /* written by ISR (producer) */
    volatile uint32_t tail; /* written by drain (consumer) */
    volatile uint32_t overflow;
} FoxFusionRing;

volatile bool fox_fusion_armed = false;
static FoxFusionRing* s_int_ring = NULL;
static FoxFusionRing* s_ext_ring = NULL;

static void fox_fusion_ring_reset(FoxFusionRing* r) {
    r->head = 0;
    r->tail = 0;
    r->overflow = 0;
}

bool fox_fusion_arm(void) {
    if(fox_fusion_armed) return true;

    /* DWT CYCCNT is already enabled at boot by furi_hal_cortex_init; make sure. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    if(s_int_ring == NULL) s_int_ring = malloc(sizeof(FoxFusionRing));
    if(s_ext_ring == NULL) s_ext_ring = malloc(sizeof(FoxFusionRing));
    if(s_int_ring == NULL || s_ext_ring == NULL) {
        fox_fusion_free();
        return false;
    }
    fox_fusion_ring_reset(s_int_ring);
    fox_fusion_ring_reset(s_ext_ring);

    __DMB();
    fox_fusion_armed = true;
    return true;
}

void fox_fusion_disarm(void) {
    fox_fusion_armed = false;
    __DMB();
}

void fox_fusion_free(void) {
    fox_fusion_armed = false;
    __DMB();
    if(s_int_ring != NULL) {
        free(s_int_ring);
        s_int_ring = NULL;
    }
    if(s_ext_ring != NULL) {
        free(s_ext_ring);
        s_ext_ring = NULL;
    }
}

static inline void fox_fusion_push(FoxFusionRing* r, uint32_t ts, bool level) {
    if(r == NULL) return;
    uint32_t head = r->head;
    uint32_t next = (head + 1u) & FOX_FUSION_RING_MASK;
    if(next == r->tail) {
        r->overflow++;
        return;
    }
    r->buf[head] = (FoxFusionEdge)((ts & ~1u) | (level ? 1u : 0u));
    __DMB();
    r->head = next;
}

void fox_fusion_push_int(bool level) {
    fox_fusion_push(s_int_ring, DWT->CYCCNT, level);
}

void fox_fusion_push_ext(bool level) {
    fox_fusion_push(s_ext_ring, DWT->CYCCNT, level);
}

static size_t fox_fusion_drain(FoxFusionRing* r, FoxFusionEdge* out, size_t max) {
    if(r == NULL || out == NULL) return 0;
    size_t n = 0;
    uint32_t tail = r->tail;
    uint32_t head = r->head;
    while(n < max && tail != head) {
        out[n++] = r->buf[tail];
        tail = (tail + 1u) & FOX_FUSION_RING_MASK;
    }
    __DMB();
    r->tail = tail;
    return n;
}

size_t fox_fusion_drain_int(FoxFusionEdge* out, size_t max) {
    return fox_fusion_drain(s_int_ring, out, max);
}

size_t fox_fusion_drain_ext(FoxFusionEdge* out, size_t max) {
    return fox_fusion_drain(s_ext_ring, out, max);
}

uint32_t fox_fusion_overflow_int(void) {
    return (s_int_ring != NULL) ? s_int_ring->overflow : 0;
}

uint32_t fox_fusion_overflow_ext(void) {
    return (s_ext_ring != NULL) ? s_ext_ring->overflow : 0;
}

static size_t fox_fusion_count(FoxFusionRing* r) {
    if(r == NULL) return 0;
    return (size_t)((r->head - r->tail) & FOX_FUSION_RING_MASK);
}

size_t fox_fusion_count_int(void) {
    return fox_fusion_count(s_int_ring);
}

size_t fox_fusion_count_ext(void) {
    return fox_fusion_count(s_ext_ring);
}
