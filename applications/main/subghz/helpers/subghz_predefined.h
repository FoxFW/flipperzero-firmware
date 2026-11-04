#pragma once

#include <furi.h>
#include <lib/subghz/types.h>
#include <lib/subghz/subghz_setting.h>

/* One predefined radio profile loaded on demand from the SD database. */
typedef struct {
    char* name; /* heap string shown in the selector */
    SubGhzRadioPreset* preset; /* name/data/data_size/frequency; NULL for None/error */
    uint16_t index; /* 0 = None */
} SubGhzPredefined;

/** Count of predefined entries in the SD database (0 on error/none). */
uint16_t subghz_predefined_get_count(void);

/** Load predefined entry `index` (0 = None) into `entry` (recycled in place).
 * Returns true on a real entry; false for None/error (entry->name becomes a placeholder,
 * entry->preset is freed to NULL). `count` is the DB count (chooses the placeholder text).
 * `settings` resolves built-in preset names to preset data. */
bool subghz_predefined_get_by_index(
    SubGhzPredefined* entry,
    uint16_t index,
    uint16_t count,
    SubGhzSetting* settings);

/** Free an entry's owned memory (name + preset) and zero it. Safe on an already-free entry. */
void subghz_predefined_free(SubGhzPredefined* entry);
