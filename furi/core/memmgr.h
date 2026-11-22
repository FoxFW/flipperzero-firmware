#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FURI_MEMMGR_GUARD 1

/* Silent RAM Recovery one-shot flag. When set, the NEXT desktop RAM-defrag
 * check is skipped entirely so an app can run its own fully controlled USB
 * reseat without the desktop's settle-loop/reseat racing it. Set by the app
 * immediately before its controlled exit; consumed (read-and-cleared) by the
 * desktop defrag check. RAM-only, no storage I/O -- safe on a low/fragmented
 * heap. Linked by every service (both set and consumer already call memmgr). */
void memmgr_defrag_suppress_once(void);
bool memmgr_defrag_suppress_consume(void);

size_t memmgr_get_free_heap(void);

size_t memmgr_get_total_heap(void);

size_t memmgr_get_minimum_free_heap(void);

void* aligned_malloc(size_t size, size_t alignment);

void aligned_free(void* p);

void* memmgr_alloc_from_pool(size_t size);

size_t memmgr_pool_get_free(void);

size_t memmgr_pool_get_max_block(void);

#ifdef __cplusplus
}
#endif
