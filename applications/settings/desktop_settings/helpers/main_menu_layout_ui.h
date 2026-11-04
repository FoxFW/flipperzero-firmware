#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * UI-side mirror of the loader's SD-backed main-menu layout.
 *
 * The firmware (applications/services/loader/loader_main_menu_layout.*) owns the
 * canonical format and writes the full default file at boot. This external
 * "Fox Settings" .fap only reads and rewrites that file, so it does NOT need
 * (and cannot link) the firmware's FLIPPER_APPS tables; it edits the generic
 * entries. The on-disk format is the contract between the two and MUST stay in
 * sync:
 *
 *   # FoxFW main menu layout v1
 *   <kind>|<enabled>|<identifier>|<label>
 *     kind     app int ext usr fox sys
 *     enabled  1 / 0
 *     ...
 */

/* MUST stay in sync with the MAIN_MENU_LAYOUT_* constants in the firmware's
 * loader_main_menu_layout.h (shared on-disk format). */
#define MM_LAYOUT_MAX        32
#define MM_LAYOUT_ID_LEN     96
#define MM_LAYOUT_LABEL_LEN  32
#define MM_APPLICATIONS_NAME "Apps"
#define MM_SUBGHZ_NAME       "Sub-GHz"

typedef enum {
    MmKindApplications = 0,
    MmKindInternal,
    MmKindExternal,
    MmKindUser,
    MmKindFoxSettings,
    MmKindSettings,
} MmKind;

typedef struct {
    MmKind kind;
    bool enabled;
    char identifier[MM_LAYOUT_ID_LEN];
    char label[MM_LAYOUT_LABEL_LEN];
} MmEntry;

typedef struct {
    MmEntry entries[MM_LAYOUT_MAX];
    uint8_t count;
} MmLayout;

/* Load the layout from the SD card. If the file is missing/empty (e.g. opened
 * before the loader ever built the menu), a minimal safe layout is synthesized
 * so the page is never empty. */
void mm_layout_load(MmLayout* layout);

/* Write the layout back to the SD card. */
void mm_layout_save(const MmLayout* layout);

/* Delete the SD file so the loader regenerates full defaults on its next menu
 * refresh (used by "Reset to Defaults"). */
void mm_layout_reset_file(void);

/* Protected = can never be hidden or removed (Apps, Sub-GHz). */
bool mm_layout_entry_protected(const MmEntry* e);

/* Deletable = user-added external app only. */
bool mm_layout_entry_deletable(const MmEntry* e);

/* Human label for display (handles empty labels on specials). */
const char* mm_layout_entry_display(const MmEntry* e);

/* Reorder: dir < 0 moves up, dir > 0 moves down. No-op at the ends. */
void mm_layout_move(MmLayout* layout, uint8_t index, int dir);

/* Remove the entry at index (caller ensures it is deletable). */
void mm_layout_remove(MmLayout* layout, uint8_t index);

/* Insert a user-added external app (by .fap path) before the Settings block.
 * Returns false if full or a duplicate path already exists. */
bool mm_layout_add_user(MmLayout* layout, const char* path, const char* label);

#ifdef __cplusplus
}
#endif
