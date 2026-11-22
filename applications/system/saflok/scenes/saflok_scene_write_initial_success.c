#include "../saflok_app_i.h"

void saflok_scene_write_initial_success_popup_callback(void* context) {
    SaflokApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventViewExit);
}

void saflok_scene_write_initial_success_on_enter(void* context) {
    SaflokApp* app = context;
    (void)0; /* dolphin_deed trimmed (Fox has no dolphin service) */

    notification_message(app->notifications, &sequence_success);

    Popup* popup = app->popup;
    popup_set_header(popup, "Success!", 75, 10, AlignLeft, AlignTop);
    popup_set_icon(popup, 0, 9, &I_DolphinSuccess_91x55);
    popup_set_timeout(popup, 1500);
    popup_set_context(popup, app);
    popup_set_callback(popup, saflok_scene_write_initial_success_popup_callback);
    popup_enable_timeout(popup);

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewPopup);
}

bool saflok_scene_write_initial_success_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SaflokCustomEventViewExit) {
            consumed = scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, SaflokSceneStart);
        }
    }
    return consumed;
}

void saflok_scene_write_initial_success_on_exit(void* context) {
    SaflokApp* app = context;

    popup_reset(app->popup);
}
