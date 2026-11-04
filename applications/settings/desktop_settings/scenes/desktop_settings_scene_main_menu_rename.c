#include <furi.h>
#include <gui/scene_manager.h>
#include <gui/modules/text_input.h>
#include <string.h>

#include "../desktop_settings_app.h"
#include "desktop_settings_scene.h"
#include "desktop_settings_scene_i.h"
#include "../helpers/main_menu_layout_ui.h"

typedef enum {
    RenameResultOk,
} RenameResult;

static void desktop_settings_scene_main_menu_rename_text_input_callback(void* context) {
    DesktopSettingsApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, RenameResultOk);
}

static bool desktop_settings_scene_main_menu_rename_validator(
    const char* text,
    FuriString* error,
    void* context) {
    UNUSED(context);
    for(; *text; ++text) {
        if(*text == '|') {
            furi_string_printf(error, "Can't use\nthe '|'\ncharacter!");
            return false;
        }
    }
    return true;
}

void desktop_settings_scene_main_menu_rename_on_enter(void* context) {
    DesktopSettingsApp* app = context;
    TextInput* text_input = app->text_input;

    uint32_t index =
        scene_manager_get_scene_state(app->scene_manager, DesktopSettingsAppSceneMainMenuRename);

    MmLayout* layout = malloc(sizeof(MmLayout));
    mm_layout_load(layout);

    app->main_menu_rename_buffer[0] = '\0';
    if(index < layout->count) {
        strlcpy(
            app->main_menu_rename_buffer,
            mm_layout_entry_display(&layout->entries[index]),
            sizeof(app->main_menu_rename_buffer));
    }
    free(layout);

    text_input_set_header_text(text_input, "Rename menu entry");
    text_input_set_validator(text_input, desktop_settings_scene_main_menu_rename_validator, NULL);
    text_input_set_minimum_length(text_input, 1);
    text_input_set_result_callback(
        text_input,
        desktop_settings_scene_main_menu_rename_text_input_callback,
        app,
        app->main_menu_rename_buffer,
        sizeof(app->main_menu_rename_buffer),
        true);

    view_dispatcher_switch_to_view(app->view_dispatcher, DesktopSettingsAppViewTextInput);
}

bool desktop_settings_scene_main_menu_rename_on_event(void* context, SceneManagerEvent event) {
    DesktopSettingsApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom && event.event == RenameResultOk) {
        uint32_t index = scene_manager_get_scene_state(
            app->scene_manager, DesktopSettingsAppSceneMainMenuRename);

        MmLayout* layout = malloc(sizeof(MmLayout));
        mm_layout_load(layout);
        if(index < layout->count) {
            strlcpy(
                layout->entries[index].label,
                app->main_menu_rename_buffer,
                sizeof(layout->entries[index].label));
            mm_layout_save(layout);
        }
        free(layout);

        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, DesktopSettingsAppSceneMainMenu);
        consumed = true;
    }

    return consumed;
}

void desktop_settings_scene_main_menu_rename_on_exit(void* context) {
    DesktopSettingsApp* app = context;
    text_input_reset(app->text_input);
}
