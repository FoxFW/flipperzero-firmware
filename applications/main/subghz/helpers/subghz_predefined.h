#pragma once

#include <furi.h>
#include <lib/subghz/types.h>
#include <lib/subghz/subghz_setting.h>

typedef struct {
    char* name;
    SubGhzRadioPreset* preset;
    uint16_t index;
} SubGhzPredefined;

uint16_t subghz_predefined_get_count(void);

bool subghz_predefined_get_by_index(
    SubGhzPredefined* entry,
    uint16_t index,
    uint16_t count,
    SubGhzSetting* settings);

void subghz_predefined_free(SubGhzPredefined* entry);
