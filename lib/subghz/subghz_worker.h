#pragma once

#include <furi_hal.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SubGhzWorker SubGhzWorker;

typedef void (*SubGhzWorkerOverrunCallback)(void* context);

typedef void (*SubGhzWorkerPairCallback)(void* context, bool level, uint32_t duration);

void subghz_worker_rx_callback(bool level, uint32_t duration, void* context);

SubGhzWorker* subghz_worker_alloc(void);

SubGhzWorker* subghz_worker_alloc_ex(size_t stream_samples);

void subghz_worker_free(SubGhzWorker* instance);

void subghz_worker_set_overrun_callback(
    SubGhzWorker* instance,
    SubGhzWorkerOverrunCallback callback);

void subghz_worker_set_pair_callback(SubGhzWorker* instance, SubGhzWorkerPairCallback callback);

void subghz_worker_set_context(SubGhzWorker* instance, void* context);

void subghz_worker_start(SubGhzWorker* instance);

void subghz_worker_stop(SubGhzWorker* instance);

bool subghz_worker_is_running(SubGhzWorker* instance);

void subghz_worker_set_filter(SubGhzWorker* instance, uint16_t timeout);

#ifdef __cplusplus
}
#endif
