#include <furi.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <storage/storage.h>
#include <dialogs/dialogs.h>
#include <flipper_application/flipper_application.h>
#include <string.h>

#include "../desktop_settings_app.h"
#include "desktop_settings_scene.h"
#include "desktop_settings_scene_i.h"
#include "../helpers/main_menu_layout_ui.h"

#define ADD_APP_INDEX (0xFFFF)
#define RESET_INDEX   (0xFFFE)

static void main_menu_derive_label(
    Storage* storage,
    const char* path,
    char* out,
    size_t out_size) {
    FuriString* path_str = furi_string_alloc_set_str(path);
    FuriString* name_str = furi_string_alloc();
    uint8_t icon_buf[FAP_MANIFEST_MAX_ICON_SIZE];
    uint8_t* icon_ptr = icon_buf;

    bool loaded = flipper_application_load_name_and_icon(path_str, storage, &icon_ptr, name_str);
    if(loaded && !furi_string_empty(name_str)) {
        strlcpy(out, furi_string_get_cstr(name_str), out_size);
    } else {
        const char* slash = strrchr(path, '/');
        const char* base = slash ? slash + 1 : path;
        strlcpy(out, base, out_size);
        size_t len = strlen(out);
        if(len > 4 && strcmp(out + len - 4, ".fap") == 0) {
            out[len - 4] = '\0';
        }
    }

    furi_string_free(path_str);
    furi_string_free(name_str);
}

static bool main_menu_selector_item_callback(
    FuriString* file_path,
    void* context,
    uint8_t** icon_ptr,
    FuriString* item_name) {
    Storage* storage = context;
    return flipper_application_load_name_and_icon(file_path, storage, icon_ptr, item_name);
}

static void desktop_settings_scene_main_menu_submenu_callback(void* context, uint32_t index) {
    DesktopSettingsApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void desktop_settings_scene_main_menu_on_enter(void* context) {
    DesktopSettingsApp* app = context;
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);

    MmLayout* layout = malloc(sizeof(MmLayout));
    mm_layout_load(layout);

    FuriString* item = furi_string_alloc();
    for(uint8_t i = 0; i < layout->count; i++) {
        const MmEntry* e = &layout->entries[i];
        furi_string_set_str(item, mm_layout_entry_display(e));
        if(!e->enabled) {
            furi_string_cat_str(item, " (off)");
        }
        submenu_add_item(
            submenu,
            furi_string_get_cstr(item),
            i,
            desktop_settings_scene_main_menu_submenu_callback,
            app);
    }
    furi_string_free(item);

    uint8_t count = layout->count;
    free(layout);

    submenu_add_item(
        submenu, "+ Add App", ADD_APP_INDEX, desktop_settings_scene_main_menu_submenu_callback, app);
    submenu_add_item(
        submenu,
        "Reset to Defaults",
        RESET_INDEX,
        desktop_settings_scene_main_menu_submenu_callback,
        app);

    submenu_set_header(submenu, "Main Menu Apps");

    uint32_t saved = scene_manager_get_scene_state(
        app->scene_manager, DesktopSettingsAppSceneMainMenu);
    if(saved < count) {
        submenu_set_selected_item(submenu, saved);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, DesktopSettingsAppViewMenu);
}

bool desktop_settings_scene_main_menu_on_event(void* context, SceneManagerEvent event) {
    DesktopSettingsApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == ADD_APP_INDEX) {
            MmLayout* layout = malloc(sizeof(MmLayout));
            mm_layout_load(layout);

            if(layout->count < MM_LAYOUT_MAX) {
                FuriString* temp_path = furi_string_alloc_set_str(EXT_PATH("apps"));
                Storage* storage = furi_record_open(RECORD_STORAGE);
                const DialogsFileBrowserOptions browser_options = {
                    .extension = ".fap",
                    .icon = NULL,
                    .skip_assets = true,
                    .hide_ext = true,
                    .item_loader_callback = main_menu_selector_item_callback,
                    .item_loader_context = storage,
                    .base_path = EXT_PATH("apps"),
                };

                bool picked =
                    dialog_file_browser_show(app->dialogs, temp_path, temp_path, &browser_options);
                if(picked) {
                    const char* path = furi_string_get_cstr(temp_path);
                    char label[MM_LAYOUT_LABEL_LEN];
                    main_menu_derive_label(storage, path, label, sizeof(label));
                    mm_layout_add_user(layout, path, label);
                    mm_layout_save(layout);
                }
                furi_record_close(RECORD_STORAGE);
                furi_string_free(temp_path);
            }

            free(layout);
            scene_manager_set_scene_state(app->scene_manager, DesktopSettingsAppSceneMainMenu, 0);
            submenu_reset(app->submenu);
            desktop_settings_scene_main_menu_on_enter(app);
            consumed = true;
        } else if(event.event == RESET_INDEX) {
            /* Delete the SD layout; the loader regenerates full defaults on its
             * next menu refresh (after this app exits). */
            mm_layout_reset_file();
            scene_manager_set_scene_state(app->scene_manager, DesktopSettingsAppSceneMainMenu, 0);
            scene_manager_previous_scene(app->scene_manager);
            consumed = true;
        } else {
            scene_manager_set_scene_state(
                app->scene_manager, DesktopSettingsAppSceneMainMenu, event.event);
            scene_manager_set_scene_state(
                app->scene_manager, DesktopSettingsAppSceneMainMenuActions, event.event);
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneMainMenuActions);
            consumed = true;
        }
    }

    return consumed;
}

void desktop_settings_scene_main_menu_on_exit(void* context) {
    DesktopSettingsApp* app = context;
    submenu_reset(app->submenu);
}
