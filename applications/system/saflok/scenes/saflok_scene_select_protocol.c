#include "../saflok_app_i.h"

#undef TAG
#define TAG "SaflokSceneSelectProtocol"

void saflok_scene_select_protocol_submenu_callback(void* context, uint32_t index) {
    SaflokApp* app = context;

    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void saflok_scene_select_protocol_on_enter(void* context) {
    SaflokApp* app = context;
    FURI_LOG_D(TAG, "on_enter");
    Submenu* submenu = app->submenu;

    FuriString* temp_str = furi_string_alloc();
    const char* prefix;
    if(scene_manager_has_previous_scene(app->scene_manager, SaflokSceneExtraActions)) {
        prefix = "Read";
        saflok_detected_protocols_fill_all_protocols(app->detected_protocols);
    } else {
        prefix = "Read as";
        submenu_set_header(submenu, "Multi-protocol card");
    }

    for(uint32_t i = 0; i < saflok_detected_protocols_get_num(app->detected_protocols); i++) {
        furi_string_printf(
            temp_str,
            "%s %s",
            prefix,
            nfc_device_get_protocol_name(
                saflok_detected_protocols_get_protocol(app->detected_protocols, i)));

        furi_string_replace_str(temp_str, "Mifare", "MIFARE");
        submenu_add_item(
            submenu,
            furi_string_get_cstr(temp_str),
            i,
            saflok_scene_select_protocol_submenu_callback,
            app);
    }
    furi_string_free(temp_str);

    submenu_set_selected_item(
        submenu, saflok_detected_protocols_get_selected_idx(app->detected_protocols));

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewMenu);
}

bool saflok_scene_select_protocol_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    FURI_LOG_D(TAG, "on_event");
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        saflok_detected_protocols_select(app->detected_protocols, event.event);
        scene_manager_next_scene(app->scene_manager, SaflokSceneRead);
        consumed = true;
    } else if(event.type == SceneManagerEventTypeBack) {
        if(scene_manager_has_previous_scene(app->scene_manager, SaflokSceneDetect)) {
            consumed = scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, SaflokSceneStart);
        }
    }
    return consumed;
}

void saflok_scene_select_protocol_on_exit(void* context) {
    SaflokApp* app = context;
    FURI_LOG_D(TAG, "on_exit");

    submenu_reset(app->submenu);
}
