#include <furi.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <string.h>

#include "../desktop_settings_app.h"
#include "desktop_settings_scene.h"
#include "desktop_settings_scene_i.h"
#include "../helpers/main_menu_layout_ui.h"

typedef enum {
    MainMenuActionToggle,
    MainMenuActionMoveUp,
    MainMenuActionMoveDown,
    MainMenuActionRename,
    MainMenuActionDelete,
    MainMenuActionCancel,
} MainMenuAction;

static void desktop_settings_scene_main_menu_actions_submenu_callback(
    void* context,
    uint32_t index) {
    DesktopSettingsApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void desktop_settings_scene_main_menu_actions_on_enter(void* context) {
    DesktopSettingsApp* app = context;
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);

    uint32_t index =
        scene_manager_get_scene_state(app->scene_manager, DesktopSettingsAppSceneMainMenuActions);

    MmLayout* layout = malloc(sizeof(MmLayout));
    mm_layout_load(layout);

    char header[MM_LAYOUT_LABEL_LEN + 4] = "App";
    bool protect = false;
    bool deletable = false;
    bool enabled = true;
    if(index < layout->count) {
        const MmEntry* e = &layout->entries[index];
        strlcpy(header, mm_layout_entry_display(e), sizeof(header));
        protect = mm_layout_entry_protected(e);
        deletable = mm_layout_entry_deletable(e);
        enabled = e->enabled;
    }
    free(layout);

    if(!protect) {
        submenu_add_item(
            submenu,
            enabled ? "Hide" : "Show",
            MainMenuActionToggle,
            desktop_settings_scene_main_menu_actions_submenu_callback,
            app);
    }
    submenu_add_item(
        submenu,
        "Move Up",
        MainMenuActionMoveUp,
        desktop_settings_scene_main_menu_actions_submenu_callback,
        app);
    submenu_add_item(
        submenu,
        "Move Down",
        MainMenuActionMoveDown,
        desktop_settings_scene_main_menu_actions_submenu_callback,
        app);
    submenu_add_item(
        submenu,
        "Rename",
        MainMenuActionRename,
        desktop_settings_scene_main_menu_actions_submenu_callback,
        app);
    if(deletable) {
        submenu_add_item(
            submenu,
            "Delete",
            MainMenuActionDelete,
            desktop_settings_scene_main_menu_actions_submenu_callback,
            app);
    }
    submenu_add_item(
        submenu,
        "Cancel",
        MainMenuActionCancel,
        desktop_settings_scene_main_menu_actions_submenu_callback,
        app);

    submenu_set_header(submenu, header);
    view_dispatcher_switch_to_view(app->view_dispatcher, DesktopSettingsAppViewMenu);
}

bool desktop_settings_scene_main_menu_actions_on_event(void* context, SceneManagerEvent event) {
    DesktopSettingsApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        uint32_t index = scene_manager_get_scene_state(
            app->scene_manager, DesktopSettingsAppSceneMainMenuActions);

        if(event.event == MainMenuActionRename) {
            scene_manager_set_scene_state(
                app->scene_manager, DesktopSettingsAppSceneMainMenuRename, index);
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneMainMenuRename);
            return true;
        }

        if(event.event != MainMenuActionCancel) {
            MmLayout* layout = malloc(sizeof(MmLayout));
            mm_layout_load(layout);

            if(index < layout->count) {
                MmEntry* e = &layout->entries[index];
                switch(event.event) {
                case MainMenuActionToggle:
                    if(!mm_layout_entry_protected(e)) {
                        e->enabled = !e->enabled;
                        mm_layout_save(layout);
                    }
                    break;
                case MainMenuActionMoveUp:
                    mm_layout_move(layout, (uint8_t)index, -1);
                    mm_layout_save(layout);
                    break;
                case MainMenuActionMoveDown:
                    mm_layout_move(layout, (uint8_t)index, +1);
                    mm_layout_save(layout);
                    break;
                case MainMenuActionDelete:
                    if(mm_layout_entry_deletable(e)) {
                        mm_layout_remove(layout, (uint8_t)index);
                        mm_layout_save(layout);
                    }
                    break;
                default:
                    break;
                }
            }
            free(layout);
        }

        scene_manager_previous_scene(app->scene_manager);
        consumed = true;
    }

    return consumed;
}

void desktop_settings_scene_main_menu_actions_on_exit(void* context) {
    DesktopSettingsApp* app = context;
    submenu_reset(app->submenu);
}
