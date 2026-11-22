#include "../saflok_app_i.h"

#undef TAG
#define TAG "SaflokSceneStart"

enum SubmenuIndex {
    SubmenuIndexRead,
    SubmenuIndexSaved,
    SubmenuIndexExit,
};

void saflok_scene_start_submenu_callback(void* context, uint32_t index) {
    SaflokApp* app = context;

    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void saflok_scene_start_on_enter(void* context) {
    SaflokApp* app = context;
    
    FURI_LOG_D(TAG, "on_enter");

    Submenu* start_menu = app->start_menu;

    FURI_LOG_D(TAG, "furi_string_reset");
    furi_string_reset(app->file_name);
    FURI_LOG_D(TAG, "nfc_device_clear");
    nfc_device_clear(app->saflok_device);
    FURI_LOG_D(TAG, "iso14443_3a_reset");
    FURI_LOG_D(TAG, "saflok_detected_protocols_reset");
    saflok_detected_protocols_reset(app->detected_protocols);

    FURI_LOG_D(TAG, "submenu_set_header");
    submenu_set_header(start_menu, "       Saflok Editor");
    FURI_LOG_D(TAG, "submenu_add_item");
    submenu_add_item(start_menu, "Read", SubmenuIndexRead, saflok_scene_start_submenu_callback, app);
    FURI_LOG_D(TAG, "submenu_add_item");
    submenu_add_item(start_menu, "Saved", SubmenuIndexSaved, saflok_scene_start_submenu_callback, app);
    FURI_LOG_D(TAG, "submenu_add_item");
    submenu_add_item(start_menu, "Exit", SubmenuIndexExit, saflok_scene_start_submenu_callback, app);

    FURI_LOG_D(TAG, "submenu_set_selected_item");
    submenu_set_selected_item(start_menu, scene_manager_get_scene_state(app->scene_manager, SaflokSceneStart));

    FURI_LOG_D(TAG, "view_dispatcher_switch_to_view");
    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewStartMenu);
}

bool saflok_scene_start_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    FURI_LOG_D(TAG, "on_event");
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubmenuIndexRead) {
            scene_manager_set_scene_state(app->scene_manager, SaflokSceneStart, SubmenuIndexRead);
            scene_manager_next_scene(app->scene_manager, SaflokSceneDetect);
            (void)0; /* dolphin_deed trimmed (Fox has no dolphin service) */
            consumed = true;
        } else if(event.event == SubmenuIndexSaved) {
            scene_manager_set_scene_state(app->scene_manager, SaflokSceneStart, SubmenuIndexSaved);
            scene_manager_next_scene(app->scene_manager, SaflokSceneFileSelect);
            consumed = true;
        } else if(event.event == SubmenuIndexExit) {
            scene_manager_stop(app->scene_manager);
            view_dispatcher_stop(app->view_dispatcher);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        scene_manager_stop(app->scene_manager);
        view_dispatcher_stop(app->view_dispatcher);
        consumed = true;
    }
    return consumed;
}

void saflok_scene_start_on_exit(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_exit");
    
    submenu_reset(app->start_menu);
}
