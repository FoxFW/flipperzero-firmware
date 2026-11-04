#include "../desktop_settings_app.h"
#include "desktop_settings_scene.h"
#include "desktop_settings_scene_i.h"
#include <storage/storage.h>
#include <dialogs/dialogs.h>
#include <assets_icons.h>

#define APPS_COUNT (0)

#define DEFAULT_INDEX         (0)
#define EXTERNAL_BROWSER_NAME ("Apps Menu (Default)")
#define FFB_DEFAULT_NAME      ("FFB (Default)") /* RightShort empty-default launches ffb.fap */

#define NONE_APPLICATION_INDEX (1)
#define NONE_APPLICATION_NAME  "None (disable)"

#define EXTERNAL_APPLICATION_INDEX (2)
#define EXTERNAL_APPLICATION_NAME  "[Select App]"

#define MAIN_LIST_APPLICATION_OFFSET (3)

#define PRESELECTED_SPECIAL 0xffffffff

static const char* favorite_fap_get_app_name(size_t i) {
    UNUSED(i);
    return NULL;
}

static bool favorite_fap_selector_item_callback(
    FuriString* file_path,
    void* context,
    uint8_t** icon_ptr,
    FuriString* item_name) {

    UNUSED(file_path);
    UNUSED(context);
    UNUSED(icon_ptr);
    UNUSED(item_name);
    return false;
}

static bool favorite_fap_selector_file_exists(char* file_path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool exists = storage_file_exists(storage, file_path);
    furi_record_close(RECORD_STORAGE);
    return exists;
}

static void desktop_settings_scene_favorite_submenu_callback(void* context, uint32_t index) {
    DesktopSettingsApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void desktop_settings_scene_favorite_on_enter(void* context) {
    DesktopSettingsApp* app = context;
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);

    uint32_t favorite_id =
        scene_manager_get_scene_state(app->scene_manager, DesktopSettingsAppSceneFavorite);
    uint32_t pre_select_item = PRESELECTED_SPECIAL;
    furi_assert(favorite_id < FavoriteAppNumber);
    FavoriteApp* curr_favorite_app = &app->settings.favorite_apps[favorite_id];
    bool default_ffb =
        (favorite_id == FavoriteAppRightShort) || (favorite_id == FavoriteAppRightLong);

    submenu_add_item(
        submenu,
        default_ffb ? (FFB_DEFAULT_NAME) : (EXTERNAL_BROWSER_NAME),
        DEFAULT_INDEX,
        desktop_settings_scene_favorite_submenu_callback,
        app);

    submenu_add_item(
        submenu,
        NONE_APPLICATION_NAME,
        NONE_APPLICATION_INDEX,
        desktop_settings_scene_favorite_submenu_callback,
        app);

    submenu_add_item(
        submenu,
        EXTERNAL_APPLICATION_NAME,
        EXTERNAL_APPLICATION_INDEX,
        desktop_settings_scene_favorite_submenu_callback,
        app);

    if(pre_select_item == PRESELECTED_SPECIAL) {
        if(curr_favorite_app->name_or_path[0] == '\0') {
            pre_select_item = DEFAULT_INDEX;
        } else if(
            (curr_favorite_app->name_or_path[1] == '\0') &&
            (curr_favorite_app->name_or_path[0] == '?')) {
            pre_select_item = NONE_APPLICATION_INDEX;
        } else {
            pre_select_item = EXTERNAL_APPLICATION_INDEX;
        }
    }

    submenu_set_header(submenu, "Favorite App");
    submenu_set_selected_item(submenu, pre_select_item);

    view_dispatcher_switch_to_view(app->view_dispatcher, DesktopSettingsAppViewMenu);
}

bool desktop_settings_scene_favorite_on_event(void* context, SceneManagerEvent event) {
    DesktopSettingsApp* app = context;
    bool consumed = false;
    FuriString* temp_path = furi_string_alloc_set_str(EXT_PATH("apps"));

    uint32_t favorite_id =
        scene_manager_get_scene_state(app->scene_manager, DesktopSettingsAppSceneFavorite);
    furi_assert(favorite_id < FavoriteAppNumber);
    FavoriteApp* curr_favorite_app = &app->settings.favorite_apps[favorite_id];

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == DEFAULT_INDEX) {
            curr_favorite_app->name_or_path[0] = '\0';
            consumed = true;
        } else if(event.event == NONE_APPLICATION_INDEX) {
            curr_favorite_app->name_or_path[0] = '?';
            curr_favorite_app->name_or_path[1] = '\0';
            consumed = true;
        } else if(event.event == EXTERNAL_APPLICATION_INDEX) {
            const DialogsFileBrowserOptions browser_options = {
                .extension = ".fap",
                .icon = NULL,
                .skip_assets = true,
                .hide_ext = true,
                .item_loader_callback = favorite_fap_selector_item_callback,
                .item_loader_context = app,
                .base_path = EXT_PATH("apps"),
            };

            if(favorite_fap_selector_file_exists(curr_favorite_app->name_or_path)) {
                furi_string_set_str(temp_path, curr_favorite_app->name_or_path);
            }

            if(dialog_file_browser_show(app->dialogs, temp_path, temp_path, &browser_options)) {
                submenu_reset(app->submenu);
                strlcpy(
                    curr_favorite_app->name_or_path,
                    furi_string_get_cstr(temp_path),
                    sizeof(curr_favorite_app->name_or_path));
                consumed = true;
            }
        } else {
            size_t app_index = event.event - MAIN_LIST_APPLICATION_OFFSET;
            const char* name = favorite_fap_get_app_name(app_index);
            if(name)
                strlcpy(
                    curr_favorite_app->name_or_path,
                    name,
                    sizeof(curr_favorite_app->name_or_path));
            consumed = true;
        }
        if(consumed) {
            scene_manager_previous_scene(app->scene_manager);
        };
        consumed = true;

        desktop_settings_save(&app->settings);
    }

    furi_string_free(temp_path);
    return consumed;
}

void desktop_settings_scene_favorite_on_exit(void* context) {
    DesktopSettingsApp* app = context;
    submenu_reset(app->submenu);
}
