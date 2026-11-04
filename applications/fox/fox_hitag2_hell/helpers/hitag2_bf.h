#pragma once

#include <furi.h>
#include <stdint.h>
#include <stdbool.h>
#include <storage/storage.h>

#define SUBGHZ_HITAG2_BF_MAX_CAPTURES 8U
#define SUBGHZ_HITAG2_BF_SD_DICT_PATH "apps_data/subghz/assets/fiat_hitag2_keys.txt"

typedef struct {
    uint32_t uid;
    uint16_t control;
    uint8_t button;
    uint32_t hop;
} SubGhzHitag2BfCapture;

typedef enum {
    SubGhzHitag2BfLevelIdle = 0,
    SubGhzHitag2BfLevelKnown = 1,
    SubGhzHitag2BfLevelFlashDict = 2,
    SubGhzHitag2BfLevelSDDict = 3,
    SubGhzHitag2BfLevelHeuristic = 4,
    SubGhzHitag2BfLevelHitag2Hell = 5,
    SubGhzHitag2BfLevelDone = 6,
} SubGhzHitag2BfLevel;

typedef struct SubGhzHitag2Bf SubGhzHitag2Bf;

typedef bool (*SubGhzHitag2BfProgressCallback)(
    uint8_t level,
    const char* level_name,
    uint8_t progress,
    uint64_t keys_tested,
    void* context);

SubGhzHitag2Bf* subghz_hitag2_bf_alloc(void);
void subghz_hitag2_bf_free(SubGhzHitag2Bf* instance);

bool subghz_hitag2_bf_add_capture(
    SubGhzHitag2Bf* instance,
    uint32_t uid,
    uint16_t control,
    uint8_t button,
    uint32_t hop);

uint8_t subghz_hitag2_bf_get_capture_count(const SubGhzHitag2Bf* instance);
uint32_t subghz_hitag2_bf_get_uid(const SubGhzHitag2Bf* instance);

void subghz_hitag2_bf_set_levels(SubGhzHitag2Bf* instance, uint8_t levels_mask);

/* Request the running brute abort. Sets the volatile cancel flag the L5 hot loop polls, so a
 * long Hitag2Hell sweep stops within ~one inner iteration instead of only at a progress tick. */
void subghz_hitag2_bf_request_cancel(SubGhzHitag2Bf* instance);

bool subghz_hitag2_bf_run(
    SubGhzHitag2Bf* instance,
    SubGhzHitag2BfProgressCallback progress_cb,
    void* context);

bool subghz_hitag2_bf_get_result(
    const SubGhzHitag2Bf* instance,
    uint8_t key_out[6],
    uint32_t* epoch_out,
    uint8_t* level_out);

uint64_t subghz_hitag2_bf_get_total_keys_tested(const SubGhzHitag2Bf* instance);

bool subghz_hitag2_bf_verify_multi(
    const SubGhzHitag2Bf* instance,
    const uint8_t key[6],
    uint32_t epoch);

uint32_t subghz_hitag2_bf_flash_dict_size(void);
const uint8_t (*subghz_hitag2_bf_flash_dict_get(uint32_t index))[6];
