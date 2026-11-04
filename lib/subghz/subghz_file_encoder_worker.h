#pragma once

#include <furi_hal.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*SubGhzFileEncoderWorkerCallbackEnd)(void* context);

typedef struct SubGhzFileEncoderWorker SubGhzFileEncoderWorker;

void subghz_file_encoder_worker_callback_end(
    SubGhzFileEncoderWorker* instance,
    SubGhzFileEncoderWorkerCallbackEnd callback_end,
    void* context_end);

SubGhzFileEncoderWorker* subghz_file_encoder_worker_alloc(void);

void subghz_file_encoder_worker_free(SubGhzFileEncoderWorker* instance);

void subghz_file_encoder_worker_get_text_progress(
    SubGhzFileEncoderWorker* instance,
    FuriString* output);

uint8_t subghz_file_encoder_worker_get_progress(SubGhzFileEncoderWorker* instance);

LevelDuration subghz_file_encoder_worker_get_level_duration(void* context);

bool subghz_file_encoder_worker_start(
    SubGhzFileEncoderWorker* instance,
    const char* file_path,
    const char* radio_device_name);

void subghz_file_encoder_worker_stop(SubGhzFileEncoderWorker* instance);

bool subghz_file_encoder_worker_is_running(SubGhzFileEncoderWorker* instance);

#ifdef __cplusplus
}
#endif
