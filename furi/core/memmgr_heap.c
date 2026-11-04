/*
 * FreeRTOS Kernel V11.1.0
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

#include "memmgr_heap.h"
#include "check.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stm32wbxx.h>
#include <stm32wb55_linker.h>
#include <core/log.h>
#include <core/common_defines.h>
#include <core/kernel.h>

#define MPU_WRAPPERS_INCLUDED_FROM_API_FILE

#include <FreeRTOS.h>
#include <task.h>

#undef MPU_WRAPPERS_INCLUDED_FROM_API_FILE

#if(configSUPPORT_DYNAMIC_ALLOCATION == 0)
#error This file must not be used if configSUPPORT_DYNAMIC_ALLOCATION is 0
#endif

#ifndef configHEAP_CLEAR_MEMORY_ON_FREE
#define configHEAP_CLEAR_MEMORY_ON_FREE 0
#endif

#define heapMINIMUM_BLOCK_SIZE ((size_t)(xHeapStructSize << 1))

#define heapBITS_PER_BYTE ((size_t)8)

#define heapSIZE_MAX (~((size_t)0))

#define heapMULTIPLY_WILL_OVERFLOW(a, b) (((a) > 0) && ((b) > (heapSIZE_MAX / (a))))

#define heapADD_WILL_OVERFLOW(a, b) ((a) > (heapSIZE_MAX - (b)))

#define heapSUBTRACT_WILL_UNDERFLOW(a, b) ((a) < (b))

#define heapBLOCK_ALLOCATED_BITMASK         (((size_t)1) << ((sizeof(size_t) * heapBITS_PER_BYTE) - 1))
#define heapBLOCK_SIZE_IS_VALID(xBlockSize) (((xBlockSize) & heapBLOCK_ALLOCATED_BITMASK) == 0)
#define heapBLOCK_IS_ALLOCATED(pxBlock) \
    (((pxBlock->xBlockSize) & heapBLOCK_ALLOCATED_BITMASK) != 0)
#define heapALLOCATE_BLOCK(pxBlock) ((pxBlock->xBlockSize) |= heapBLOCK_ALLOCATED_BITMASK)
#define heapFREE_BLOCK(pxBlock)     ((pxBlock->xBlockSize) &= ~heapBLOCK_ALLOCATED_BITMASK)

uint8_t* ucHeap = (uint8_t*)&__heap_start__;

typedef struct A_BLOCK_LINK {
    struct A_BLOCK_LINK* pxNextFreeBlock;
    size_t xBlockSize;
} BlockLink_t;

#if(configENABLE_HEAP_PROTECTOR == 1)

extern void vApplicationGetRandomHeapCanary(portPOINTER_SIZE_TYPE* pxHeapCanary);

PRIVILEGED_DATA static portPOINTER_SIZE_TYPE xHeapCanary;

#define heapPROTECT_BLOCK_POINTER(pxBlock) \
    ((BlockLink_t*)(((portPOINTER_SIZE_TYPE)(pxBlock)) ^ xHeapCanary))
#else

#define heapPROTECT_BLOCK_POINTER(pxBlock) (pxBlock)

#endif

#define heapVALIDATE_BLOCK_POINTER(pxBlock)      \
    configASSERT(                                \
        ((uint8_t*)(pxBlock) >= &(ucHeap[0])) && \
        ((uint8_t*)(pxBlock) <= &(ucHeap[configTOTAL_HEAP_SIZE - 1])))

static void prvInsertBlockIntoFreeList(BlockLink_t* pxBlockToInsert) PRIVILEGED_FUNCTION;

static void prvHeapInit(void) PRIVILEGED_FUNCTION;

static const size_t xHeapStructSize = (sizeof(BlockLink_t) + ((size_t)(portBYTE_ALIGNMENT - 1))) &
                                      ~((size_t)portBYTE_ALIGNMENT_MASK);

PRIVILEGED_DATA static BlockLink_t xStart;
PRIVILEGED_DATA static BlockLink_t* pxEnd = NULL;

PRIVILEGED_DATA static size_t xFreeBytesRemaining = (size_t)0U;
PRIVILEGED_DATA static size_t xMinimumEverFreeBytesRemaining = (size_t)0U;
PRIVILEGED_DATA static size_t xNumberOfSuccessfulAllocations = (size_t)0U;
PRIVILEGED_DATA static size_t xNumberOfSuccessfulFrees = (size_t)0U;

#include <m-dict.h>

DICT_DEF2(MemmgrHeapAllocDict, uint32_t, uint32_t)

DICT_DEF2(
    MemmgrHeapThreadDict,
    uint32_t,
    M_DEFAULT_OPLIST,
    MemmgrHeapAllocDict_t,
    DICT_OPLIST(MemmgrHeapAllocDict))

static MemmgrHeapThreadDict_t memmgr_heap_thread_dict = {0};
static volatile uint32_t memmgr_heap_thread_trace_depth = 0;

/* ===== FOX_HI_TRACE (DIAGNOSTIC -- REMOVE BEFORE RELEASE) ====================
 * Records every allocation whose user pointer lands at/above FOX_HI_TRACE_ADDR
 * (thread that made it + size + tick) and drops it again on free, so the table
 * holds only the SURVIVORS living high in the heap. Dumped in the fragmentation
 * map as "High-heap survivors" to name the owners of the first-boot stranded
 * comb and show WHEN each was born (boot vs the wizard/slideshow window). Pure
 * observability: static storage, no heap use, runs under the existing trace
 * depth guard. Comment the #define to compile out. */
/* #define FOX_HI_TRACE 1 */ /* stripped for release: reclaims ~12KB .bss */
#ifdef FOX_HI_TRACE
#define FOX_HI_TRACE_ADDR 0x20020000u /* raised: chase mid-heap app-use strands, not boot band */
#define FOX_HI_TRACE_MAX  192
typedef struct {
    uint32_t addr; /* user pointer; 0 = empty slot */
    uint32_t size;
    uint32_t tick;
    uint32_t caller; /* return addr of the malloc caller (addr2line this) */
    char thread[16];
} FoxHiEntry;
static FoxHiEntry fox_hi_tbl[FOX_HI_TRACE_MAX];
static uint32_t fox_hi_dropped = 0;

static void fox_hi_record(void* pointer, size_t size, void* caller) {
    uint32_t a = (uint32_t)pointer;
    if(a < FOX_HI_TRACE_ADDR) return;
    for(uint32_t i = 0; i < FOX_HI_TRACE_MAX; i++) {
        if(fox_hi_tbl[i].addr == 0) {
            fox_hi_tbl[i].addr = a;
            fox_hi_tbl[i].size = (uint32_t)size;
            fox_hi_tbl[i].tick = (uint32_t)furi_get_tick();
            fox_hi_tbl[i].caller = (uint32_t)caller;
            const char* nm = furi_thread_get_name(furi_thread_get_current_id());
            uint32_t k = 0;
            if(nm != NULL) {
                for(; nm[k] != '\0' && k < sizeof(fox_hi_tbl[i].thread) - 1; k++) {
                    fox_hi_tbl[i].thread[k] = nm[k];
                }
            }
            fox_hi_tbl[i].thread[k] = '\0';
            return;
        }
    }
    fox_hi_dropped++;
}

static void fox_hi_forget(void* pointer) {
    uint32_t a = (uint32_t)pointer;
    if(a < FOX_HI_TRACE_ADDR) return;
    for(uint32_t i = 0; i < FOX_HI_TRACE_MAX; i++) {
        if(fox_hi_tbl[i].addr == a) {
            fox_hi_tbl[i].addr = 0;
            return;
        }
    }
}

/* Append-only log of allocations >= FOX_HI_TRACE_ADDR made during the boot
 * window, NEVER removed on free. Entries here that are NOT live survivors are
 * the TRANSIENTS that churned the high band during boot -- e.g. the first-boot-
 * only allocation that pushes desktop's permanent blocks high. addr2line the
 * caller to name them. */
#define FOX_BOOT_WINDOW 6000u
#define FOX_BOOT_BIG    2048u /* also capture big allocs (the spike buffer), any address */
#define FOX_BOOT_MAX    192
typedef struct {
    uint32_t addr;
    uint32_t size;
    uint32_t tick;
    uint32_t caller;
    char thread[16];
} FoxBootEntry;
static FoxBootEntry fox_boot_tbl[FOX_BOOT_MAX];
static uint32_t fox_boot_count = 0;
static uint32_t fox_boot_dropped = 0;

static void fox_boot_record(void* pointer, size_t size, void* caller) {
    uint32_t a = (uint32_t)pointer;
    /* Record high-address allocs (footprint growth) OR any big alloc (a spike
     * buffer, which may land LOW and push everything above it up, then free).
     * The first-boot RAM spike that strands the permanent blocks can be either
     * shape, so catch both. */
    if(a < FOX_HI_TRACE_ADDR && size < FOX_BOOT_BIG) return;
    uint32_t t = (uint32_t)furi_get_tick();
    if(t >= FOX_BOOT_WINDOW) return;
    if(fox_boot_count >= FOX_BOOT_MAX) {
        fox_boot_dropped++;
        return;
    }
    uint32_t i = fox_boot_count++;
    fox_boot_tbl[i].addr = a;
    fox_boot_tbl[i].size = (uint32_t)size;
    fox_boot_tbl[i].tick = t;
    fox_boot_tbl[i].caller = (uint32_t)caller;
    const char* nm = furi_thread_get_name(furi_thread_get_current_id());
    uint32_t k = 0;
    if(nm != NULL) {
        for(; nm[k] != '\0' && k < sizeof(fox_boot_tbl[i].thread) - 1; k++) {
            fox_boot_tbl[i].thread[k] = nm[k];
        }
    }
    fox_boot_tbl[i].thread[k] = '\0';
}
#endif /* FOX_HI_TRACE */

void memmgr_heap_init(void) {
    MemmgrHeapThreadDict_init(memmgr_heap_thread_dict);
}

void memmgr_heap_enable_thread_trace(FuriThreadId thread_id) {
    vTaskSuspendAll();
    {
        memmgr_heap_thread_trace_depth++;
        furi_check(MemmgrHeapThreadDict_get(memmgr_heap_thread_dict, (uint32_t)thread_id) == NULL);
        MemmgrHeapAllocDict_t alloc_dict;
        MemmgrHeapAllocDict_init(alloc_dict);
        MemmgrHeapThreadDict_set_at(memmgr_heap_thread_dict, (uint32_t)thread_id, alloc_dict);
        MemmgrHeapAllocDict_clear(alloc_dict);
        memmgr_heap_thread_trace_depth--;
    }
    (void)xTaskResumeAll();
}

void memmgr_heap_disable_thread_trace(FuriThreadId thread_id) {
    vTaskSuspendAll();
    {
        memmgr_heap_thread_trace_depth++;
        furi_check(MemmgrHeapThreadDict_erase(memmgr_heap_thread_dict, (uint32_t)thread_id));
        memmgr_heap_thread_trace_depth--;
    }
    (void)xTaskResumeAll();
}

size_t memmgr_heap_get_thread_memory(FuriThreadId thread_id) {
    size_t leftovers = MEMMGR_HEAP_UNKNOWN;
    vTaskSuspendAll();
    {
        memmgr_heap_thread_trace_depth++;
        MemmgrHeapAllocDict_t* alloc_dict =
            MemmgrHeapThreadDict_get(memmgr_heap_thread_dict, (uint32_t)thread_id);
        if(alloc_dict) {
            leftovers = 0;
            MemmgrHeapAllocDict_it_t alloc_dict_it;
            for(MemmgrHeapAllocDict_it(alloc_dict_it, *alloc_dict);
                !MemmgrHeapAllocDict_end_p(alloc_dict_it);
                MemmgrHeapAllocDict_next(alloc_dict_it)) {
                MemmgrHeapAllocDict_itref_t* data = MemmgrHeapAllocDict_ref(alloc_dict_it);
                if(data->key != 0) {
                    uint8_t* puc = (uint8_t*)data->key;
                    puc -= xHeapStructSize;
                    BlockLink_t* pxLink = (void*)puc;

                    if((pxLink->xBlockSize & heapBLOCK_ALLOCATED_BITMASK) &&
                       pxLink->pxNextFreeBlock == heapPROTECT_BLOCK_POINTER(NULL)) {
                        leftovers += data->value;
                    }
                }
            }
        }
        memmgr_heap_thread_trace_depth--;
    }
    (void)xTaskResumeAll();
    return leftovers;
}

#undef traceMALLOC
static inline void traceMALLOC(void* pointer, size_t size, void* caller) {
    FuriThreadId thread_id = furi_thread_get_current_id();
    if(thread_id && memmgr_heap_thread_trace_depth == 0) {
        memmgr_heap_thread_trace_depth++;
#ifdef FOX_HI_TRACE
        fox_hi_record(pointer, size, caller);
        fox_boot_record(pointer, size, caller);
#else
        (void)caller;
#endif
        MemmgrHeapAllocDict_t* alloc_dict =
            MemmgrHeapThreadDict_get(memmgr_heap_thread_dict, (uint32_t)thread_id);
        if(alloc_dict) {
            MemmgrHeapAllocDict_set_at(*alloc_dict, (uint32_t)pointer, (uint32_t)size);
        }
        memmgr_heap_thread_trace_depth--;
    }
}

#undef traceFREE
static inline void traceFREE(void* pointer, size_t size) {
    UNUSED(size);
    FuriThreadId thread_id = furi_thread_get_current_id();
    if(thread_id && memmgr_heap_thread_trace_depth == 0) {
        memmgr_heap_thread_trace_depth++;
#ifdef FOX_HI_TRACE
        fox_hi_forget(pointer);
#endif
        MemmgrHeapAllocDict_t* alloc_dict =
            MemmgrHeapThreadDict_get(memmgr_heap_thread_dict, (uint32_t)thread_id);
        if(alloc_dict) {

            const bool res = MemmgrHeapAllocDict_erase(*alloc_dict, (uint32_t)pointer);
            UNUSED(res);
        }
        memmgr_heap_thread_trace_depth--;
    }
}

size_t memmgr_heap_get_max_free_block(void) {
    HeapStats_t heap_stats;
    vPortGetHeapStats(&heap_stats);
    return heap_stats.xSizeOfLargestFreeBlockInBytes;
}

void memmgr_heap_printf_free_blocks(void) {
    BlockLink_t* pxBlock;
    const uint8_t* heap_base = ucHeap;
    const uint8_t* heap_limit = ucHeap + (size_t)configTOTAL_HEAP_SIZE;

    pxBlock = heapPROTECT_BLOCK_POINTER(xStart.pxNextFreeBlock);
    while(pxBlock != NULL &&
          (const uint8_t*)pxBlock >= heap_base && (const uint8_t*)pxBlock < heap_limit) {
        heapVALIDATE_BLOCK_POINTER(pxBlock);
        printf("A %p S %lu\r\n", (void*)pxBlock, (uint32_t)pxBlock->xBlockSize);
        pxBlock = heapPROTECT_BLOCK_POINTER(pxBlock->pxNextFreeBlock);
    }
}

#define MEMMGR_HEAP_MAP_BUCKET_COUNT    128
#define MEMMGR_HEAP_MAP_BUCKETS_PER_ROW 32
#define MEMMGR_HEAP_MAP_LINE_SIZE       160
/* Per-thread breakdown collects into a fixed stack array (no alloc under the
 * scheduler-suspend), so it is capped. 24 covers every live Flipper thread with
 * headroom; any beyond that are counted as an overflow line, not dropped
 * silently. Keep the struct small - this sits on the RAM-monitor / CLI stack. */
#define MEMMGR_HEAP_MAP_MAX_THREADS     24

/* Clamp-and-emit a formatted line built with snprintf(). `written` is
 * snprintf()'s return value (the length it WOULD have written, which can
 * exceed the buffer if the line got truncated); mirrors the clamp used by
 * the loader/VCP/RAM-monitor debug-log helpers elsewhere in this codebase. */
static void memmgr_heap_map_emit(
    MemmgrHeapMapWriteCallback write_callback,
    void* write_context,
    const char* line,
    int written) {
    if(write_callback == NULL || written <= 0) {
        return;
    }
    size_t write_len = (size_t)written;
    if(write_len > (size_t)(MEMMGR_HEAP_MAP_LINE_SIZE - 1)) {
        write_len = (size_t)(MEMMGR_HEAP_MAP_LINE_SIZE - 1);
    }
    write_callback(write_context, line, write_len);
}

/* Size of the gap between `prev_end` and `block_addr`, or 0 if there is none.
 * Shared by the stats pass and the used-region-emitting pass so both agree
 * on exactly what counts as a non-empty used region. */
static size_t memmgr_heap_map_gap_size(const uint8_t* prev_end, const uint8_t* block_addr) {
    if(block_addr > prev_end) {
        return (size_t)(block_addr - prev_end);
    }
    return 0;
}

/* One density character per bucket, by how much of the bucket is used. */
static char memmgr_heap_map_density_char(size_t used, size_t width) {
    if(width == 0 || used == 0) {
        return ' ';
    }
    if(used >= width) {
        return '#';
    }
    if(used * 4 <= width) {
        return '.';
    }
    if(used * 4 <= width * 2) {
        return ':';
    }
    if(used * 4 <= width * 3) {
        return '+';
    }
    return '*';
}


void memmgr_heap_write_fragmentation_map(
    MemmgrHeapMapWriteCallback write_callback,
    void* write_context,
    const char* label) {
    char line[MEMMGR_HEAP_MAP_LINE_SIZE];
    int written;

    const uint8_t* heap_base = ucHeap;
    const size_t heap_total = (size_t)configTOTAL_HEAP_SIZE;
    const uint8_t* heap_limit = heap_base + heap_total;

    /* ---- Pass 1: walk the free list once to compute every summary number
     * before writing anything -- the header (written first) needs them. */
    size_t free_block_count = 0;
    size_t largest_free_block = 0;
    size_t sum_free_from_walk = 0;
    size_t used_region_count = 0;
    size_t largest_used_region = 0;
    size_t sum_used_from_gaps = 0;
    size_t smallest_free_block = (size_t)-1; /* sentinel; set to 0 below if no free blocks */
    size_t second_largest_free_block = 0;
    /* Free-block size histogram (coalescing health at a glance): a heap with
     * plenty of total free but a tall <=64 column is fragmented into crumbs. */
    size_t free_hist[6] = {0, 0, 0, 0, 0, 0}; /* <=32, <=64, <=256, <=1K, <=4K, >4K */

    {
        BlockLink_t* pxBlock = heapPROTECT_BLOCK_POINTER(xStart.pxNextFreeBlock);
        const uint8_t* prev_end = heap_base;

        while(pxBlock != NULL && pxBlock != pxEnd &&
              (const uint8_t*)pxBlock >= heap_base && (const uint8_t*)pxBlock < heap_limit) {
            heapVALIDATE_BLOCK_POINTER(pxBlock);
            const uint8_t* block_addr = (const uint8_t*)pxBlock;
            size_t block_size = pxBlock->xBlockSize;

            size_t gap = memmgr_heap_map_gap_size(prev_end, block_addr);
            if(gap > 0) {
                used_region_count++;
                sum_used_from_gaps += gap;
                if(gap > largest_used_region) {
                    largest_used_region = gap;
                }
            }

            free_block_count++;
            sum_free_from_walk += block_size;
            if(block_size > largest_free_block) {
                second_largest_free_block = largest_free_block;
                largest_free_block = block_size;
            } else if(block_size > second_largest_free_block) {
                second_largest_free_block = block_size;
            }
            if(block_size < smallest_free_block) {
                smallest_free_block = block_size;
            }
            if(block_size <= 32) {
                free_hist[0]++;
            } else if(block_size <= 64) {
                free_hist[1]++;
            } else if(block_size <= 256) {
                free_hist[2]++;
            } else if(block_size <= 1024) {
                free_hist[3]++;
            } else if(block_size <= 4096) {
                free_hist[4]++;
            } else {
                free_hist[5]++;
            }

            prev_end = block_addr + block_size;
            pxBlock = heapPROTECT_BLOCK_POINTER(pxBlock->pxNextFreeBlock);
        }

        size_t tail_gap = memmgr_heap_map_gap_size(prev_end, heap_limit);
        if(tail_gap > 0) {
            used_region_count++;
            sum_used_from_gaps += tail_gap;
            if(tail_gap > largest_used_region) {
                largest_used_region = tail_gap;
            }
        }
    }

    if(free_block_count == 0) smallest_free_block = 0;
    size_t avg_free_block = (free_block_count > 0) ? (sum_free_from_walk / free_block_count) : 0;

    size_t total_free = xPortGetFreeHeapSize();
    size_t total_used = (heap_total > total_free) ? (heap_total - total_free) : 0;
    size_t accounted_total = sum_free_from_walk + sum_used_from_gaps;

    uint32_t tick = furi_get_tick();
    uint32_t freq = furi_kernel_get_tick_frequency();
    uint32_t uptime_s = (freq > 0) ? (tick / freq) : 0;

    /* ---- Header + summary ---- */
    written = snprintf(line, sizeof(line), "==== FoxFW Heap Fragmentation Map ====\n");
    memmgr_heap_map_emit(write_callback, write_context, line, written);

    written = snprintf(
        line,
        sizeof(line),
        "Tick: %lu (uptime %luh%lum%lus)\n",
        (unsigned long)tick,
        (unsigned long)(uptime_s / 3600),
        (unsigned long)((uptime_s / 60) % 60),
        (unsigned long)(uptime_s % 60));
    memmgr_heap_map_emit(write_callback, write_context, line, written);

    if(label != NULL && label[0] != '\0') {
        written = snprintf(line, sizeof(line), "Label: %s\n", label);
        memmgr_heap_map_emit(write_callback, write_context, line, written);
    }

    written = snprintf(
        line,
        sizeof(line),
        "Heap base: 0x%08lx  Heap end: 0x%08lx  Heap size: %zu bytes\n",
        (unsigned long)(size_t)heap_base,
        (unsigned long)(size_t)heap_limit,
        heap_total);
    memmgr_heap_map_emit(write_callback, write_context, line, written);

    written = snprintf(line, sizeof(line), "\n--- Summary ---\n");
    memmgr_heap_map_emit(write_callback, write_context, line, written);

    written = snprintf(
        line, sizeof(line), "Total free bytes (xPortGetFreeHeapSize): %zu\n", total_free);
    memmgr_heap_map_emit(write_callback, write_context, line, written);

    written = snprintf(
        line,
        sizeof(line),
        "Total free bytes (sum of free list, cross-check): %zu %s\n",
        sum_free_from_walk,
        (sum_free_from_walk == total_free) ? "[OK]" : "[MISMATCH - heap changed mid-scan?]");
    memmgr_heap_map_emit(write_callback, write_context, line, written);

    written = snprintf(
        line, sizeof(line), "Total used bytes (heap size - total free): %zu\n", total_used);
    memmgr_heap_map_emit(write_callback, write_context, line, written);

    written = snprintf(line, sizeof(line), "Free blocks: %zu\n", free_block_count);
    memmgr_heap_map_emit(write_callback, write_context, line, written);

    written = snprintf(line, sizeof(line), "Used regions: %zu\n", used_region_count);
    memmgr_heap_map_emit(write_callback, write_context, line, written);

    written = snprintf(line, sizeof(line), "Largest free block: %zu bytes\n", largest_free_block);
    memmgr_heap_map_emit(write_callback, write_context, line, written);

    written =
        snprintf(line, sizeof(line), "Largest used region: %zu bytes\n", largest_used_region);
    memmgr_heap_map_emit(write_callback, write_context, line, written);

    if(largest_free_block > 0) {
        double frag_ratio = (double)total_free / (double)largest_free_block;
        written = snprintf(
            line,
            sizeof(line),
            "Fragmentation ratio (total free / largest free block): %.2f\n",
            frag_ratio);
    } else {
        written = snprintf(
            line,
            sizeof(line),
            "Fragmentation ratio (total free / largest free block): N/A (no free blocks)\n");
    }
    memmgr_heap_map_emit(write_callback, write_context, line, written);

    written = snprintf(
        line,
        sizeof(line),
        "Self-check: free-list sum %zu + used-region sum %zu = %zu\n",
        sum_free_from_walk,
        sum_used_from_gaps,
        accounted_total);
    memmgr_heap_map_emit(write_callback, write_context, line, written);

    written = snprintf(
        line,
        sizeof(line),
        "Self-check: heap size is %zu %s\n",
        heap_total,
        (accounted_total == heap_total) ? "[OK]" : "[MISMATCH - bug or corruption]");
    memmgr_heap_map_emit(write_callback, write_context, line, written);

    /* ---- Extended summary ----
     * All cheap, all always-safe (plain reads of file-scope counters + the Pass-1
     * accumulators). These are the numbers that turn a one-off snapshot into a
     * diagnosis: the low-water mark tells you the worst this boot ever got even
     * though it recovered; the alloc/free churn tells you whether a "leak" is
     * really growth or just high turnover; the free-block distribution tells you
     * whether free heap is one usable slab or a drift of unusable crumbs. */
    written = snprintf(
        line,
        sizeof(line),
        "Min-ever free (low-water since boot): %zu bytes\n",
        (size_t)xMinimumEverFreeBytesRemaining);
    memmgr_heap_map_emit(write_callback, write_context, line, written);

    {
        size_t peak_used = (heap_total > (size_t)xMinimumEverFreeBytesRemaining) ?
                               (heap_total - (size_t)xMinimumEverFreeBytesRemaining) :
                               0;
        written = snprintf(
            line, sizeof(line), "Peak used (high-water since boot): %zu bytes\n", peak_used);
        memmgr_heap_map_emit(write_callback, write_context, line, written);
    }

    written = snprintf(
        line,
        sizeof(line),
        "Lifetime allocs: %zu, frees: %zu (net live blocks: %zu)\n",
        (size_t)xNumberOfSuccessfulAllocations,
        (size_t)xNumberOfSuccessfulFrees,
        (size_t)(xNumberOfSuccessfulAllocations - xNumberOfSuccessfulFrees));
    memmgr_heap_map_emit(write_callback, write_context, line, written);

    written = snprintf(
        line,
        sizeof(line),
        "Free block sizes: smallest %zu, avg %zu, 2nd-largest %zu, largest %zu\n",
        smallest_free_block,
        avg_free_block,
        second_largest_free_block,
        largest_free_block);
    memmgr_heap_map_emit(write_callback, write_context, line, written);

    written = snprintf(
        line,
        sizeof(line),
        "Free blocks by size: <=32:%zu <=64:%zu <=256:%zu <=1K:%zu <=4K:%zu >4K:%zu\n",
        free_hist[0],
        free_hist[1],
        free_hist[2],
        free_hist[3],
        free_hist[4],
        free_hist[5]);
    memmgr_heap_map_emit(write_callback, write_context, line, written);

    if(heap_total > 0) {
        written = snprintf(
            line,
            sizeof(line),
            "Heap utilisation: used %lu.%01lu%%, free %lu.%01lu%%\n",
            (unsigned long)(total_used * 100u / heap_total),
            (unsigned long)((total_used * 1000u / heap_total) % 10u),
            (unsigned long)(total_free * 100u / heap_total),
            (unsigned long)((total_free * 1000u / heap_total) % 10u));
        memmgr_heap_map_emit(write_callback, write_context, line, written);
    }

    if(total_free > 0) {
        written = snprintf(
            line,
            sizeof(line),
            "Largest free block is %lu.%01lu%% of all free heap (100%% = unfragmented)\n",
            (unsigned long)(largest_free_block * 100u / total_free),
            (unsigned long)((largest_free_block * 1000u / total_free) % 10u));
        memmgr_heap_map_emit(write_callback, write_context, line, written);
    }

    written = snprintf(
        line,
        sizeof(line),
        "\nNOTE: a \"used region\" is only an address range not covered by any free\n");
    memmgr_heap_map_emit(write_callback, write_context, line, written);
    written = snprintf(
        line,
        sizeof(line),
        "block. The heap does not tag allocations with a caller, so this file\n");
    memmgr_heap_map_emit(write_callback, write_context, line, written);
    written = snprintf(
        line,
        sizeof(line),
        "cannot say WHAT is using a region, only WHERE it is and HOW BIG. Several\n");
    memmgr_heap_map_emit(write_callback, write_context, line, written);
    written = snprintf(
        line, sizeof(line), "adjacent allocations with no free gap show up as one region.\n");
    memmgr_heap_map_emit(write_callback, write_context, line, written);

    /* ---- Pass 2: free block list. prvInsertBlockIntoFreeList() keeps this
     * list in ascending-address order, so walking it forward is already
     * sorted -- nothing to sort. ---- */
    written = snprintf(line, sizeof(line), "\n--- Free blocks (sorted by address) ---\n");
    memmgr_heap_map_emit(write_callback, write_context, line, written);

    {
        BlockLink_t* pxBlock = heapPROTECT_BLOCK_POINTER(xStart.pxNextFreeBlock);
        while(pxBlock != NULL && pxBlock != pxEnd &&
              (const uint8_t*)pxBlock >= heap_base && (const uint8_t*)pxBlock < heap_limit) {
            heapVALIDATE_BLOCK_POINTER(pxBlock);
            written = snprintf(
                line,
                sizeof(line),
                "0x%08lx  %8zu bytes\n",
                (unsigned long)(size_t)pxBlock,
                (size_t)pxBlock->xBlockSize);
            memmgr_heap_map_emit(write_callback, write_context, line, written);
            pxBlock = heapPROTECT_BLOCK_POINTER(pxBlock->pxNextFreeBlock);
        }
    }

    /* ---- Pass 3: used-region list, same gap logic as pass 1 (via
     * memmgr_heap_map_gap_size()) so the two can never disagree, this time
     * emitting a line instead of just measuring. ---- */
    written = snprintf(line, sizeof(line), "\n--- Used regions (sorted by address) ---\n");
    memmgr_heap_map_emit(write_callback, write_context, line, written);

    {
        BlockLink_t* pxBlock = heapPROTECT_BLOCK_POINTER(xStart.pxNextFreeBlock);
        const uint8_t* prev_end = heap_base;

        while(pxBlock != NULL && pxBlock != pxEnd &&
              (const uint8_t*)pxBlock >= heap_base && (const uint8_t*)pxBlock < heap_limit) {
            heapVALIDATE_BLOCK_POINTER(pxBlock);
            const uint8_t* block_addr = (const uint8_t*)pxBlock;
            size_t gap = memmgr_heap_map_gap_size(prev_end, block_addr);
            if(gap > 0) {
                written = snprintf(
                    line,
                    sizeof(line),
                    "0x%08lx - 0x%08lx (%8zu bytes)\n",
                    (unsigned long)(size_t)prev_end,
                    (unsigned long)(size_t)block_addr,
                    gap);
                memmgr_heap_map_emit(write_callback, write_context, line, written);
            }
            prev_end = block_addr + pxBlock->xBlockSize;
            pxBlock = heapPROTECT_BLOCK_POINTER(pxBlock->pxNextFreeBlock);
        }

        size_t tail_gap = memmgr_heap_map_gap_size(prev_end, heap_limit);
        if(tail_gap > 0) {
            written = snprintf(
                line,
                sizeof(line),
                "0x%08lx - 0x%08lx (%8zu bytes)\n",
                (unsigned long)(size_t)prev_end,
                (unsigned long)(size_t)heap_limit,
                tail_gap);
            memmgr_heap_map_emit(write_callback, write_context, line, written);
        }
    }

    /* ---- Pass 4: compact visual map. Divide the heap into a fixed number
     * of address buckets (the last one absorbs whatever remainder doesn't
     * divide evenly) and, for each bucket, walk the free list again (still
     * cheap -- the same short list as above) summing how much of that
     * bucket's byte range is covered by a free block. This is
     * O(buckets * free_blocks), trivial for the sizes involved here, and
     * much easier to check by inspection than merging two sorted sequences
     * would be. ---- */
    {
        size_t bucket_count = MEMMGR_HEAP_MAP_BUCKET_COUNT;
        if(bucket_count > heap_total) {
            bucket_count = (heap_total > 0) ? heap_total : 1;
        }
        size_t bucket_size = heap_total / bucket_count;
        if(bucket_size == 0) {
            bucket_size = 1;
        }

        written = snprintf(
            line,
            sizeof(line),
            "\n--- Visual map (%zu buckets, ~%zu bytes each) ---\n",
            bucket_count,
            bucket_size);
        memmgr_heap_map_emit(write_callback, write_context, line, written);

        written = snprintf(
            line,
            sizeof(line),
            "Legend: ' '=free  .=<=25%% used  :=<=50%%  +=<=75%%  *=<100%%  #=100%% used\n");
        memmgr_heap_map_emit(write_callback, write_context, line, written);

        size_t buckets_per_row = MEMMGR_HEAP_MAP_BUCKETS_PER_ROW;
        if(buckets_per_row > bucket_count) {
            buckets_per_row = bucket_count;
        }

        size_t bucket_index = 0;
        while(bucket_index < bucket_count) {
            const uint8_t* row_addr = heap_base + bucket_index * bucket_size;
            int prefix_len =
                snprintf(line, sizeof(line), "0x%08lx |", (unsigned long)(size_t)row_addr);
            size_t pos = (prefix_len > 0) ? (size_t)prefix_len : 0;
            if(pos > sizeof(line) - 1) {
                pos = sizeof(line) - 1;
            }

            size_t col = 0;
            /* Leave room for the trailing '\n' plus a spare byte: stop
             * accepting more bucket characters once `pos` gets within 4
             * bytes of the end of `line`. */
            while(col < buckets_per_row && bucket_index < bucket_count &&
                  pos < sizeof(line) - 4) {
                const uint8_t* bucket_begin = heap_base + bucket_index * bucket_size;
                const uint8_t* bucket_end = (bucket_index + 1 == bucket_count) ?
                                                 heap_limit :
                                                 heap_base + (bucket_index + 1) * bucket_size;
                size_t width = (size_t)(bucket_end - bucket_begin);
                size_t free_in_bucket = 0;

                BlockLink_t* pxBlock = heapPROTECT_BLOCK_POINTER(xStart.pxNextFreeBlock);
                while(pxBlock != NULL && pxBlock != pxEnd &&
                      (const uint8_t*)pxBlock >= heap_base && (const uint8_t*)pxBlock < heap_limit) {
                    heapVALIDATE_BLOCK_POINTER(pxBlock);
                    const uint8_t* block_addr = (const uint8_t*)pxBlock;
                    const uint8_t* block_end = block_addr + pxBlock->xBlockSize;
                    const uint8_t* overlap_start =
                        (block_addr > bucket_begin) ? block_addr : bucket_begin;
                    const uint8_t* overlap_end =
                        (block_end < bucket_end) ? block_end : bucket_end;
                    if(overlap_end > overlap_start) {
                        free_in_bucket += (size_t)(overlap_end - overlap_start);
                    }
                    pxBlock = heapPROTECT_BLOCK_POINTER(pxBlock->pxNextFreeBlock);
                }

                size_t used_in_bucket = (free_in_bucket < width) ? (width - free_in_bucket) : 0;
                line[pos] = memmgr_heap_map_density_char(used_in_bucket, width);
                pos++;

                bucket_index++;
                col++;
            }

            line[pos] = '\n';
            pos++;
            memmgr_heap_map_emit(write_callback, write_context, line, (int)pos);
        }
    }

    /* ---- Per-thread live heap usage ----
     * The allocator already tags every live allocation with the FuriThreadId that
     * made it (the MemmgrHeapThreadDict that traceMALLOC/traceFREE maintain), so
     * we can attribute live heap to the OWNING THREAD - the single most useful
     * thing when hunting a leak or a RAM hog. Collected under ONE brief
     * vTaskSuspendAll() (the outer dict and the TCBs are only stable while the
     * scheduler is suspended, and file I/O must never run there), into a fixed
     * stack array, then emitted after resume. The per-block validity test mirrors
     * the proven one in memmgr_heap_get_thread_memory(). */
    {
        struct {
            uint32_t bytes;
            uint32_t count;
            char     name[20];
        } tinfo[MEMMGR_HEAP_MAP_MAX_THREADS];
        uint32_t tcount = 0;
        uint32_t toverflow = 0;

        vTaskSuspendAll();
        {
            memmgr_heap_thread_trace_depth++;
            MemmgrHeapThreadDict_it_t it;
            for(MemmgrHeapThreadDict_it(it, memmgr_heap_thread_dict);
                !MemmgrHeapThreadDict_end_p(it);
                MemmgrHeapThreadDict_next(it)) {
                MemmgrHeapThreadDict_itref_t* tref = MemmgrHeapThreadDict_ref(it);
                if(tcount >= MEMMGR_HEAP_MAP_MAX_THREADS) {
                    toverflow++;
                    continue;
                }
                uint32_t sum = 0;
                uint32_t cnt = 0;
                MemmgrHeapAllocDict_it_t ait;
                for(MemmgrHeapAllocDict_it(ait, tref->value);
                    !MemmgrHeapAllocDict_end_p(ait);
                    MemmgrHeapAllocDict_next(ait)) {
                    MemmgrHeapAllocDict_itref_t* data = MemmgrHeapAllocDict_ref(ait);
                    if(data->key != 0) {
                        uint8_t* puc = (uint8_t*)data->key;
                        puc -= xHeapStructSize;
                        BlockLink_t* pxLink = (void*)puc;
                        if((pxLink->xBlockSize & heapBLOCK_ALLOCATED_BITMASK) &&
                           pxLink->pxNextFreeBlock == heapPROTECT_BLOCK_POINTER(NULL)) {
                            sum += data->value;
                            cnt++;
                        }
                    }
                }
                tinfo[tcount].bytes = sum;
                tinfo[tcount].count = cnt;
                const char* nm = furi_thread_get_name((FuriThreadId)(size_t)tref->key);
                uint32_t k = 0;
                if(nm != NULL) {
                    for(; nm[k] != '\0' && k < sizeof(tinfo[tcount].name) - 1; k++) {
                        tinfo[tcount].name[k] = nm[k];
                    }
                }
                tinfo[tcount].name[k] = '\0';
                tcount++;
            }
            memmgr_heap_thread_trace_depth--;
        }
        (void)xTaskResumeAll();

        written = snprintf(
            line, sizeof(line), "\n--- Per-thread live heap (tracked allocations) ---\n");
        memmgr_heap_map_emit(write_callback, write_context, line, written);

        size_t tracked_total = 0;
        for(uint32_t i = 0; i < tcount; i++) {
            tracked_total += tinfo[i].bytes;
            written = snprintf(
                line,
                sizeof(line),
                "%-20s %8lu bytes  %4lu allocs\n",
                tinfo[i].name[0] ? tinfo[i].name : "<unnamed>",
                (unsigned long)tinfo[i].bytes,
                (unsigned long)tinfo[i].count);
            memmgr_heap_map_emit(write_callback, write_context, line, written);
        }

        written = snprintf(
            line,
            sizeof(line),
            "Tracked live total: %zu bytes across %lu threads%s\n",
            tracked_total,
            (unsigned long)tcount,
            toverflow ? " (+more; table full)" : "");
        memmgr_heap_map_emit(write_callback, write_context, line, written);

        written = snprintf(
            line,
            sizeof(line),
            "NOTE: per-thread totals cover only heap the thread-trace tagged; early-boot\n");
        memmgr_heap_map_emit(write_callback, write_context, line, written);
        written = snprintf(
            line,
            sizeof(line),
            "and ISR allocations are untagged, so this sums to less than total used - it\n");
        memmgr_heap_map_emit(write_callback, write_context, line, written);
        written =
            snprintf(line, sizeof(line), "shows WHICH THREAD owns the heap it does cover.\n");
        memmgr_heap_map_emit(write_callback, write_context, line, written);
    }

#ifdef FOX_HI_TRACE
    {
        /* Bump the trace depth so traceMALLOC/traceFREE skip fox_hi_record/forget
         * while we read the table -- a stable snapshot without suspending the
         * scheduler (file I/O must be able to run here). */
        memmgr_heap_thread_trace_depth++;
        written = snprintf(
            line,
            sizeof(line),
            "\n--- High-heap survivors (>= 0x%08lx, DIAGNOSTIC) ---\n",
            (unsigned long)FOX_HI_TRACE_ADDR);
        memmgr_heap_map_emit(write_callback, write_context, line, written);
        written = snprintf(
            line,
            sizeof(line),
            "Live blocks in/above the first-boot stranded band: owner thread + birth tick.\n");
        memmgr_heap_map_emit(write_callback, write_context, line, written);

        uint32_t shown = 0;
        size_t hi_total = 0;
        uint32_t last_addr = 0; /* address-sorted selection emit (addrs are unique) */
        for(;;) {
            uint32_t best_i = FOX_HI_TRACE_MAX;
            uint32_t best_addr = 0xFFFFFFFFu;
            for(uint32_t i = 0; i < FOX_HI_TRACE_MAX; i++) {
                uint32_t a = fox_hi_tbl[i].addr;
                if(a != 0 && a > last_addr && a < best_addr) {
                    best_addr = a;
                    best_i = i;
                }
            }
            if(best_i == FOX_HI_TRACE_MAX) break;
            last_addr = best_addr;
            hi_total += fox_hi_tbl[best_i].size;
            shown++;
            written = snprintf(
                line,
                sizeof(line),
                "0x%08lx  %8lu bytes  tick %8lu  caller 0x%08lx  %s\n",
                (unsigned long)fox_hi_tbl[best_i].addr,
                (unsigned long)fox_hi_tbl[best_i].size,
                (unsigned long)fox_hi_tbl[best_i].tick,
                (unsigned long)fox_hi_tbl[best_i].caller,
                fox_hi_tbl[best_i].thread[0] ? fox_hi_tbl[best_i].thread : "<unnamed>");
            memmgr_heap_map_emit(write_callback, write_context, line, written);
        }
        written = snprintf(
            line,
            sizeof(line),
            "High-heap survivors: %lu blocks, %zu bytes total%s\n",
            (unsigned long)shown,
            hi_total,
            fox_hi_dropped ? " (table overflowed; some missed)" : "");
        memmgr_heap_map_emit(write_callback, write_context, line, written);

        written = snprintf(
            line,
            sizeof(line),
            "\n--- Boot-window allocs (>= 0x%08lx OR >= %lu B, tick < %lu, DIAGNOSTIC) ---\n",
            (unsigned long)FOX_HI_TRACE_ADDR,
            (unsigned long)FOX_BOOT_BIG,
            (unsigned long)FOX_BOOT_WINDOW);
        memmgr_heap_map_emit(write_callback, write_context, line, written);
        written = snprintf(
            line,
            sizeof(line),
            "Allocated high during boot; an entry NOT in the survivors above was freed =\n"
            "a transient that pushed the permanent blocks high. addr2line the caller.\n");
        memmgr_heap_map_emit(write_callback, write_context, line, written);
        for(uint32_t i = 0; i < fox_boot_count; i++) {
            written = snprintf(
                line,
                sizeof(line),
                "0x%08lx  %8lu bytes  tick %8lu  caller 0x%08lx  %s\n",
                (unsigned long)fox_boot_tbl[i].addr,
                (unsigned long)fox_boot_tbl[i].size,
                (unsigned long)fox_boot_tbl[i].tick,
                (unsigned long)fox_boot_tbl[i].caller,
                fox_boot_tbl[i].thread[0] ? fox_boot_tbl[i].thread : "<unnamed>");
            memmgr_heap_map_emit(write_callback, write_context, line, written);
        }
        written = snprintf(
            line,
            sizeof(line),
            "Boot-window high allocs: %lu recorded%s\n",
            (unsigned long)fox_boot_count,
            fox_boot_dropped ? " (table overflowed; some missed)" : "");
        memmgr_heap_map_emit(write_callback, write_context, line, written);
        memmgr_heap_thread_trace_depth--;
    }
#endif /* FOX_HI_TRACE */
}

void* pvPortMalloc(size_t xWantedSize) {
    BlockLink_t* pxBlock;
    BlockLink_t* pxPreviousBlock;
    BlockLink_t* pxNewBlockLink;
    void* pvReturn = NULL;
    size_t xToWipe = xWantedSize;
    size_t xAdditionalRequiredSize;
    size_t xAllocatedBlockSize = 0;

    if(FURI_IS_IRQ_MODE()) {
        furi_crash("memmgt in ISR");
    }

    if(xWantedSize > 0) {

        if(heapADD_WILL_OVERFLOW(xWantedSize, xHeapStructSize) == 0) {
            xWantedSize += xHeapStructSize;

            if((xWantedSize & portBYTE_ALIGNMENT_MASK) != 0x00) {

                xAdditionalRequiredSize =
                    portBYTE_ALIGNMENT - (xWantedSize & portBYTE_ALIGNMENT_MASK);

                if(heapADD_WILL_OVERFLOW(xWantedSize, xAdditionalRequiredSize) == 0) {
                    xWantedSize += xAdditionalRequiredSize;
                } else {
                    xWantedSize = 0;
                }
            } else {
                mtCOVERAGE_TEST_MARKER();
            }
        } else {
            xWantedSize = 0;
        }
    } else {
        mtCOVERAGE_TEST_MARKER();
    }

    vTaskSuspendAll();
    {

        if(pxEnd == NULL) {
            prvHeapInit();
            memmgr_heap_init();
        } else {
            mtCOVERAGE_TEST_MARKER();
        }

        if(heapBLOCK_SIZE_IS_VALID(xWantedSize) != 0) {
            if((xWantedSize > 0) && (xWantedSize <= xFreeBytesRemaining)) {

                pxPreviousBlock = &xStart;
                pxBlock = heapPROTECT_BLOCK_POINTER(xStart.pxNextFreeBlock);
                heapVALIDATE_BLOCK_POINTER(pxBlock);

                while((pxBlock->xBlockSize < xWantedSize) &&
                      (pxBlock->pxNextFreeBlock != heapPROTECT_BLOCK_POINTER(NULL))) {
                    pxPreviousBlock = pxBlock;
                    pxBlock = heapPROTECT_BLOCK_POINTER(pxBlock->pxNextFreeBlock);
                    heapVALIDATE_BLOCK_POINTER(pxBlock);
                }

                if(pxBlock != pxEnd) {

                    pvReturn = (void*)(((uint8_t*)heapPROTECT_BLOCK_POINTER(
                                           pxPreviousBlock->pxNextFreeBlock)) +
                                       xHeapStructSize);
                    heapVALIDATE_BLOCK_POINTER(pvReturn);

                    pxPreviousBlock->pxNextFreeBlock = pxBlock->pxNextFreeBlock;

                    configASSERT(
                        heapSUBTRACT_WILL_UNDERFLOW(pxBlock->xBlockSize, xWantedSize) == 0);

                    if((pxBlock->xBlockSize - xWantedSize) > heapMINIMUM_BLOCK_SIZE) {

                        pxNewBlockLink = (void*)(((uint8_t*)pxBlock) + xWantedSize);
                        configASSERT((((size_t)pxNewBlockLink) & portBYTE_ALIGNMENT_MASK) == 0);

                        pxNewBlockLink->xBlockSize = pxBlock->xBlockSize - xWantedSize;
                        pxBlock->xBlockSize = xWantedSize;

                        pxNewBlockLink->pxNextFreeBlock = pxPreviousBlock->pxNextFreeBlock;
                        pxPreviousBlock->pxNextFreeBlock =
                            heapPROTECT_BLOCK_POINTER(pxNewBlockLink);
                    } else {
                        mtCOVERAGE_TEST_MARKER();
                    }

                    xFreeBytesRemaining -= pxBlock->xBlockSize;

                    if(xFreeBytesRemaining < xMinimumEverFreeBytesRemaining) {
                        xMinimumEverFreeBytesRemaining = xFreeBytesRemaining;
                    } else {
                        mtCOVERAGE_TEST_MARKER();
                    }

                    xAllocatedBlockSize = pxBlock->xBlockSize;

                    heapALLOCATE_BLOCK(pxBlock);
                    pxBlock->pxNextFreeBlock = heapPROTECT_BLOCK_POINTER(NULL);
                    xNumberOfSuccessfulAllocations++;
                } else {
                    mtCOVERAGE_TEST_MARKER();
                }
            } else {
                mtCOVERAGE_TEST_MARKER();
            }
        } else {
            mtCOVERAGE_TEST_MARKER();
        }

        traceMALLOC(pvReturn, xAllocatedBlockSize, __builtin_return_address(0));

        (void)xAllocatedBlockSize;
    }
    (void)xTaskResumeAll();

#if(configUSE_MALLOC_FAILED_HOOK == 1)
    {
        if(pvReturn == NULL) {
            vApplicationMallocFailedHook();
        } else {
            mtCOVERAGE_TEST_MARKER();
        }
    }
#endif

    configASSERT((((size_t)pvReturn) & (size_t)portBYTE_ALIGNMENT_MASK) == 0);

    furi_check(pvReturn, xWantedSize ? "out of memory" : "malloc(0)");
    pvReturn = memset(pvReturn, 0, xToWipe);
    return pvReturn;
}

void vPortFree(void* pv) {
    uint8_t* puc = (uint8_t*)pv;
    BlockLink_t* pxLink;

    if(FURI_IS_IRQ_MODE()) {
        furi_crash("memmgt in ISR");
    }

    if(pv != NULL) {

        puc -= xHeapStructSize;

        pxLink = (void*)puc;

        heapVALIDATE_BLOCK_POINTER(pxLink);
        configASSERT(heapBLOCK_IS_ALLOCATED(pxLink) != 0);
        configASSERT(pxLink->pxNextFreeBlock == heapPROTECT_BLOCK_POINTER(NULL));

        if(heapBLOCK_IS_ALLOCATED(pxLink) != 0) {
            if(pxLink->pxNextFreeBlock == heapPROTECT_BLOCK_POINTER(NULL)) {

                heapFREE_BLOCK(pxLink);
#if(configHEAP_CLEAR_MEMORY_ON_FREE == 1)
                {

                    if(heapSUBTRACT_WILL_UNDERFLOW(pxLink->xBlockSize, xHeapStructSize) == 0) {
                        (void)memset(
                            puc + xHeapStructSize, 0, pxLink->xBlockSize - xHeapStructSize);
                    }
                }
#endif

                vTaskSuspendAll();
                {
                    furi_assert((size_t)pv >= SRAM_BASE);
                    furi_assert((size_t)pv < SRAM_BASE + 1024 * 256);
                    furi_assert(pxLink->xBlockSize >= xHeapStructSize);
                    furi_assert((pxLink->xBlockSize - xHeapStructSize) < 1024 * 256);

                    xFreeBytesRemaining += pxLink->xBlockSize;
                    traceFREE(pv, pxLink->xBlockSize);
                    prvInsertBlockIntoFreeList(((BlockLink_t*)pxLink));
                    xNumberOfSuccessfulFrees++;
                }
                (void)xTaskResumeAll();
            } else {
                mtCOVERAGE_TEST_MARKER();
            }
        } else {
            mtCOVERAGE_TEST_MARKER();
        }
    }
}

size_t xPortGetFreeHeapSize(void) {
    return xFreeBytesRemaining;
}

size_t xPortGetMinimumEverFreeHeapSize(void) {
    return xMinimumEverFreeBytesRemaining;
}

void xPortResetHeapMinimumEverFreeHeapSize(void) {
    xMinimumEverFreeBytesRemaining = xFreeBytesRemaining;
}

void vPortInitialiseBlocks(void) {

}

void* pvPortCalloc(size_t xNum, size_t xSize) {
    void* pv = NULL;

    if(heapMULTIPLY_WILL_OVERFLOW(xNum, xSize) == 0) {
        pv = pvPortMalloc(xNum * xSize);

        if(pv != NULL) {
            (void)memset(pv, 0, xNum * xSize);
        }
    }

    return pv;
}

static void prvHeapInit(void)
{
    BlockLink_t* pxFirstFreeBlock;
    portPOINTER_SIZE_TYPE uxStartAddress, uxEndAddress;
    size_t xTotalHeapSize = configTOTAL_HEAP_SIZE;

    uxStartAddress = (portPOINTER_SIZE_TYPE)ucHeap;

    if((uxStartAddress & portBYTE_ALIGNMENT_MASK) != 0) {
        uxStartAddress += (portBYTE_ALIGNMENT - 1);
        uxStartAddress &= ~((portPOINTER_SIZE_TYPE)portBYTE_ALIGNMENT_MASK);
        xTotalHeapSize -= (size_t)(uxStartAddress - (portPOINTER_SIZE_TYPE)ucHeap);
    }

#if(configENABLE_HEAP_PROTECTOR == 1)
    { vApplicationGetRandomHeapCanary(&(xHeapCanary)); }
#endif

    xStart.pxNextFreeBlock = (void*)heapPROTECT_BLOCK_POINTER(uxStartAddress);
    xStart.xBlockSize = (size_t)0;

    uxEndAddress = uxStartAddress + (portPOINTER_SIZE_TYPE)xTotalHeapSize;
    uxEndAddress -= (portPOINTER_SIZE_TYPE)xHeapStructSize;
    uxEndAddress &= ~((portPOINTER_SIZE_TYPE)portBYTE_ALIGNMENT_MASK);
    pxEnd = (BlockLink_t*)uxEndAddress;
    pxEnd->xBlockSize = 0;
    pxEnd->pxNextFreeBlock = heapPROTECT_BLOCK_POINTER(NULL);

    pxFirstFreeBlock = (BlockLink_t*)uxStartAddress;
    pxFirstFreeBlock->xBlockSize =
        (size_t)(uxEndAddress - (portPOINTER_SIZE_TYPE)pxFirstFreeBlock);
    pxFirstFreeBlock->pxNextFreeBlock = heapPROTECT_BLOCK_POINTER(pxEnd);

    xMinimumEverFreeBytesRemaining = pxFirstFreeBlock->xBlockSize;
    xFreeBytesRemaining = pxFirstFreeBlock->xBlockSize;
}

static void prvInsertBlockIntoFreeList(BlockLink_t* pxBlockToInsert)
{
    BlockLink_t* pxIterator;
    uint8_t* puc;

    for(pxIterator = &xStart;
        heapPROTECT_BLOCK_POINTER(pxIterator->pxNextFreeBlock) < pxBlockToInsert;
        pxIterator = heapPROTECT_BLOCK_POINTER(pxIterator->pxNextFreeBlock)) {

    }

    if(pxIterator != &xStart) {
        heapVALIDATE_BLOCK_POINTER(pxIterator);
    }

    puc = (uint8_t*)pxIterator;

    if((puc + pxIterator->xBlockSize) == (uint8_t*)pxBlockToInsert) {
        pxIterator->xBlockSize += pxBlockToInsert->xBlockSize;
        pxBlockToInsert = pxIterator;
    } else {
        mtCOVERAGE_TEST_MARKER();
    }

    puc = (uint8_t*)pxBlockToInsert;

    if((puc + pxBlockToInsert->xBlockSize) ==
       (uint8_t*)heapPROTECT_BLOCK_POINTER(pxIterator->pxNextFreeBlock)) {
        if(heapPROTECT_BLOCK_POINTER(pxIterator->pxNextFreeBlock) != pxEnd) {

            pxBlockToInsert->xBlockSize +=
                heapPROTECT_BLOCK_POINTER(pxIterator->pxNextFreeBlock)->xBlockSize;
            pxBlockToInsert->pxNextFreeBlock =
                heapPROTECT_BLOCK_POINTER(pxIterator->pxNextFreeBlock)->pxNextFreeBlock;
        } else {
            pxBlockToInsert->pxNextFreeBlock = heapPROTECT_BLOCK_POINTER(pxEnd);
        }
    } else {
        pxBlockToInsert->pxNextFreeBlock = pxIterator->pxNextFreeBlock;
    }

    if(pxIterator != pxBlockToInsert) {
        pxIterator->pxNextFreeBlock = heapPROTECT_BLOCK_POINTER(pxBlockToInsert);
    } else {
        mtCOVERAGE_TEST_MARKER();
    }
}

void vPortGetHeapStats(HeapStats_t* pxHeapStats) {
    BlockLink_t* pxBlock;
    size_t
        xBlocks = 0,
        xMaxSize = 0,
        xMinSize =
            portMAX_DELAY;

    vTaskSuspendAll();
    {
        pxBlock = heapPROTECT_BLOCK_POINTER(xStart.pxNextFreeBlock);

        if(pxBlock != NULL) {
            while(pxBlock != pxEnd) {

                xBlocks++;

                if(pxBlock->xBlockSize > xMaxSize) {
                    xMaxSize = pxBlock->xBlockSize;
                }

                if(pxBlock->xBlockSize < xMinSize) {
                    xMinSize = pxBlock->xBlockSize;
                }

                pxBlock = heapPROTECT_BLOCK_POINTER(pxBlock->pxNextFreeBlock);
            }
        }
    }
    (void)xTaskResumeAll();

    pxHeapStats->xSizeOfLargestFreeBlockInBytes = xMaxSize;
    pxHeapStats->xSizeOfSmallestFreeBlockInBytes = xMinSize;
    pxHeapStats->xNumberOfFreeBlocks = xBlocks;

    taskENTER_CRITICAL();
    {
        pxHeapStats->xAvailableHeapSpaceInBytes = xFreeBytesRemaining;
        pxHeapStats->xNumberOfSuccessfulAllocations = xNumberOfSuccessfulAllocations;
        pxHeapStats->xNumberOfSuccessfulFrees = xNumberOfSuccessfulFrees;
        pxHeapStats->xMinimumEverFreeBytesRemaining = xMinimumEverFreeBytesRemaining;
    }
    taskEXIT_CRITICAL();
}

void vPortHeapResetState(void) {
    pxEnd = NULL;

    xFreeBytesRemaining = (size_t)0U;
    xMinimumEverFreeBytesRemaining = (size_t)0U;
    xNumberOfSuccessfulAllocations = (size_t)0U;
    xNumberOfSuccessfulFrees = (size_t)0U;
}
