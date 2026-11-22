#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAIN_MENU_LAYOUT_MAX       32
#define MAIN_MENU_LAYOUT_ID_LEN    96
#define MAIN_MENU_LAYOUT_LABEL_LEN 32
#define MAIN_MENU_LAYOUT_DIR       EXT_PATH("apps_data/fox")
#define MAIN_MENU_LAYOUT_FILE_NAME EXT_PATH("apps_data/fox/main_menu.layout")
#define MAIN_MENU_LAYOUT_HEADER    "# FoxFW main menu layout v1"

#define MAIN_MENU_APPLICATIONS_NAME "Apps"

#define MAIN_MENU_SUBGHZ_NAME "Sub-GHz"

typedef enum {
    MainMenuEntryKindApplications = 0,
    MainMenuEntryKindInternal,
    MainMenuEntryKindExternal,
    MainMenuEntryKindUser,
    MainMenuEntryKindFoxSettings,
    MainMenuEntryKindSettings,
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

bool main_menu_layout_entry_is_protected(const MainMenuLayoutEntry* entry);

const char* main_menu_layout_kind_to_token(MainMenuEntryKind kind);
bool main_menu_layout_kind_from_token(const char* token, MainMenuEntryKind* out);

bool main_menu_layout_load(MainMenuLayout* layout);

void main_menu_layout_build_default(MainMenuLayout* layout);

void main_menu_layout_save(const MainMenuLayout* layout);

typedef enum {
    MainMenuLayoutFpOk,
    MainMenuLayoutFpAbsent,
    MainMenuLayoutFpError,
} MainMenuLayoutFpStatus;

MainMenuLayoutFpStatus main_menu_layout_fingerprint(uint32_t* hash, uint32_t* len);

#ifdef __cplusplus
}
#endif
