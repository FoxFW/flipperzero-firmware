#pragma once

#include <math.h>
#include <furi.h>
#include <furi_hal.h>
#include <lib/flipper_format/flipper_format.h>
#include <lib/subghz/types.h>
#include <lib/subghz/protocols/base.h>

#define SUBGHZ_HISTORY_MAX 255

typedef enum {
    SubGhzHistorySourceNone,
    SubGhzHistorySourceInternal,
    SubGhzHistorySourceExternal,
} SubGhzHistorySource;

typedef struct SubGhzHistory SubGhzHistory;

SubGhzHistory* subghz_history_alloc(void);

void subghz_history_free(SubGhzHistory* instance);

void subghz_history_reset(SubGhzHistory* instance);

void subghz_history_delete_item(SubGhzHistory* instance, uint16_t idx);

SubGhzRadioPreset* subghz_history_get_radio_preset(SubGhzHistory* instance, uint16_t idx);

uint16_t subghz_history_get_item(SubGhzHistory* instance);

SubGhzHistorySource subghz_history_get_source(SubGhzHistory* instance, uint16_t idx);

const char* subghz_history_get_protocol_name(SubGhzHistory* instance, uint16_t idx);

DateTime subghz_history_get_datetime(SubGhzHistory* instance, uint16_t idx);

void subghz_history_get_menu_item(
    void* context,
    uint16_t idx,
    FuriString* name,
    FuriString* time,
    uint8_t* type);

bool subghz_history_get_text_space_left(SubGhzHistory* instance, FuriString* output);

uint16_t subghz_history_get_last_index(SubGhzHistory* instance);

bool subghz_history_add_to_history(
    SubGhzHistory* instance,
    void* context,
    SubGhzRadioPreset* preset,
    SubGhzHistorySource source);

bool subghz_history_replace_item(
    SubGhzHistory* instance,
    uint16_t idx,
    FlipperFormat* ff,
    uint8_t type);

int16_t subghz_history_add_raw(
    SubGhzHistory* instance,
    FlipperFormat* raw,
    SubGhzRadioPreset* preset,
    const char* label);

int16_t subghz_history_dedup_last_fiat_hitag2(SubGhzHistory* instance);

int16_t subghz_history_dedup_last_came_holtek(SubGhzHistory* instance);

int16_t subghz_history_find_identical(
    SubGhzHistory* instance,
    void* context,
    SubGhzRadioPreset* preset,
    bool* out_notify);

FlipperFormat* subghz_history_get_raw_data(SubGhzHistory* instance, uint16_t idx);

void subghz_history_release_cache(SubGhzHistory* instance);

bool subghz_history_copy_item_to_file(SubGhzHistory* instance, uint16_t idx, const char* path);

void subghz_history_flush(SubGhzHistory* instance);

void subghz_history_set_auto_save_pending(SubGhzHistory* instance, uint16_t idx, bool pending);

bool subghz_history_find_auto_save_pending(SubGhzHistory* instance, uint16_t* idx);
