#include "../saflok_app_i.h"

#undef TAG
#define TAG "SaflokSceneDeleteSuccess"

void saflok_scene_delete_success_popup_callback(void* context) {
    SaflokApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventViewExit);
}

void saflok_scene_delete_success_on_enter(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_enter");

    Popup* popup = app->popup;
    popup_set_icon(popup, 0, 2, &I_DolphinMafia_119x62);
    popup_set_header(popup, "Deleted", 80, 19, AlignLeft, AlignBottom);
    popup_set_timeout(popup, 1500);
    popup_set_context(popup, app);
    popup_set_callback(popup, saflok_scene_delete_success_popup_callback);
    popup_enable_timeout(popup);
    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewPopup);
}

bool saflok_scene_delete_success_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    
    FURI_LOG_D(TAG, "on_event");
    
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SaflokCustomEventViewExit) {
            if(scene_manager_has_previous_scene(app->scene_manager, SaflokSceneMfClassicKeys)) {
                consumed = scene_manager_search_and_switch_to_previous_scene(
                    app->scene_manager, SaflokSceneMfClassicKeys);
            } else {
                consumed = scene_manager_search_and_switch_to_previous_scene(
                    app->scene_manager, SaflokSceneFileSelect);

                if(!consumed) {
                    scene_manager_stop(app->scene_manager);
                    view_dispatcher_stop(app->view_dispatcher);
                }
            }
        }
    }
    return consumed;
}

void saflok_scene_delete_success_on_exit(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_exit");
    
    popup_reset(app->popup);
}
