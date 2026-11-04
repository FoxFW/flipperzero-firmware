#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * FoxFW "Main Menu Apps" layout.
 *
 * A single ordered, SD-card-backed description of the WHOLE main (apps) menu.
 * Replaces the old internal-flash ".main_menu.pins" (which only pinned extra
 * apps to the top and was wiped on every firmware update).
 *
 * Stored on the SD card so it survives firmware updates:
 *   EXT_PATH("apps_data/fox/main_menu.layout")
 *
 * Text format, one entry per line, human diffable:
 *   # FoxFW main menu layout v1          <- header / comment (lines starting '#')
 *   <kind>|<enabled>|<identifier>|<label>
 *
 *   kind       one of: app int ext usr fox sys  (see MainMenuEntryKind)
 *   enabled    "1" (shown) or "0" (hidden)
 *   identifier launch id: app display-name (int/ext), .fap path (usr),
 *              "Apps" (app), or "" (fox/sys)
 *   label      display text (defaults to the app name; user rename overrides)
 *
 * Neither identifier nor label may contain '|' or a newline. Paths never do;
 * the rename UI rejects '|'.
 */

/* Resident RAM = MAX * (ID_LEN + LABEL_LEN + ~8). Kept modest, but MAX must cover the
 * WHOLE menu (Apps + all internal + all external + Fox Settings + Settings ~= 15, PLUS up
 * to 12 migrated legacy pins, PLUS user additions); build_default drops any overflow, so
 * don't set it below the realistic default+pins total. 32 gives safe headroom. A real .fap
 * path is ~36 chars (96 is safe). 32*(96+32+8) ~= 4.3 KB vs the old 48*168 ~= 8 KB. MUST
 * stay in sync with the MM_LAYOUT_* constants in
 * desktop_settings/helpers/main_menu_layout_ui.h. */
#define MAIN_MENU_LAYOUT_MAX       32
#define MAIN_MENU_LAYOUT_ID_LEN    96
#define MAIN_MENU_LAYOUT_LABEL_LEN 32
#define MAIN_MENU_LAYOUT_DIR       EXT_PATH("apps_data/fox")
#define MAIN_MENU_LAYOUT_FILE_NAME EXT_PATH("apps_data/fox/main_menu.layout")
#define MAIN_MENU_LAYOUT_HEADER    "# FoxFW main menu layout v1"

/* The Applications-list entry name; must equal LOADER_APPLICATIONS_NAME. */
#define MAIN_MENU_APPLICATIONS_NAME "Apps"
/* The Sub-GHz internal app display name; both "Apps" and this are protected
 * (can never be hidden or removed) because they are the only path to those
 * apps. */
#define MAIN_MENU_SUBGHZ_NAME "Sub-GHz"

typedef enum {
    MainMenuEntryKindApplications = 0, /* the "Apps" list entry (protected)   */
    MainMenuEntryKindInternal,         /* FLIPPER_APPS entry, by display name */
    MainMenuEntryKindExternal,         /* FLIPPER_EXTERNAL_APPS entry, by name */
    MainMenuEntryKindUser,             /* user-added external .fap, by path    */
    MainMenuEntryKindFoxSettings,      /* the "Fox Settings" entry             */
    MainMenuEntryKindSettings,         /* the "Settings" submenu entry         */
} MainMenuEntryKind;

typedef struct {
    MainMenuEntryKind kind;
    bool enabled;
    char identifier[MAIN_MENU_LAYOUT_ID_LEN];
    char label[MAIN_MENU_LAYOUT_LABEL_LEN];
} MainMenuLayoutEntry;

typedef struct {
    MainMenuLayoutEntry entries[MAIN_MENU_LAYOUT_MAX];
    uint8_t count;
} MainMenuLayout;

/* true if this entry can never be hidden or removed (Apps, Sub-GHz). */
bool main_menu_layout_entry_is_protected(const MainMenuLayoutEntry* entry);

/* Serialize a kind to/from its token. */
const char* main_menu_layout_kind_to_token(MainMenuEntryKind kind);
bool main_menu_layout_kind_from_token(const char* token, MainMenuEntryKind* out);

/*
 * Load the layout from the SD card. If the file is missing or empty, the
 * current build's defaults are generated (enumerating the Applications entry,
 * FLIPPER_APPS, FLIPPER_EXTERNAL_APPS, Fox Settings and Settings, in the same
 * order the menu used before this feature), the legacy internal-flash pins are
 * migrated to the front as user entries, and the result is written back to the
 * SD card. Never fails: a missing SD simply yields the in-memory defaults.
 *
 * NOTE: default generation enumerates FLIPPER_APPS / FLIPPER_EXTERNAL_APPS and
 * therefore must run inside the firmware (the loader service), not an external
 * .fap.
 *
 * Returns true when `layout` holds a usable layout (parsed, or freshly
 * generated defaults). Returns false only when the file exists but could not be
 * read right now (transient SD error / corrupt file): the file is left intact
 * and the caller should keep whatever menu it already has.
 */
bool main_menu_layout_load(MainMenuLayout* layout);

/* Build the current build's default layout in memory (no file I/O). */
void main_menu_layout_build_default(MainMenuLayout* layout);

/* Write the layout to the SD card (best effort; creates the directory). */
void main_menu_layout_save(const MainMenuLayout* layout);

typedef enum {
    MainMenuLayoutFpOk, /* file read; *hash and *len are valid           */
    MainMenuLayoutFpAbsent, /* file does not exist                       */
    MainMenuLayoutFpError, /* file exists but could not be read just now */
} MainMenuLayoutFpStatus;

/*
 * Cheap change-detection fingerprint of the on-disk layout file: an FNV-1a hash
 * over the raw file bytes plus the byte length, streamed through a small stack
 * buffer (no allocation, no parse). Lets the menu-refresh hot path detect
 * "unchanged" without holding a second copy of the layout resident in RAM.
 */
MainMenuLayoutFpStatus main_menu_layout_fingerprint(uint32_t* hash, uint32_t* len);

#ifdef __cplusplus
}
#endif
