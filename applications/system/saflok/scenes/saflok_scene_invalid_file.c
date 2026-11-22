#include "../saflok_app_i.h"

void saflok_scene_invalid_file_popup_callback(void* context) {
    SaflokApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventViewExit);
}

void saflok_scene_invalid_file_on_enter(void* context) {
    SaflokApp* app = context;

    Popup* popup = app->popup;
    popup_set_header(popup, "Invalid file", 63, 10, AlignCenter, AlignTop);
    popup_set_text(
        popup, "Only MIFARE Classic files\nare supported", 63, 40, AlignCenter, AlignCenter);
    popup_set_context(popup, app);
    popup_set_callback(popup, saflok_scene_invalid_file_popup_callback);

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewPopup);
}

bool saflok_scene_invalid_file_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SaflokCustomEventViewExit) {
            consumed = scene_manager_previous_scene(app->scene_manager);
        }
    }

    return consumed;
}

void saflok_scene_invalid_file_on_exit(void* context) {
    SaflokApp* app = context;

    popup_reset(app->popup);
}
