#include "../saflok_app_i.h"

#undef TAG
#define TAG "SaflokSceneSaveSuccess"

void saflok_scene_save_success_popup_callback(void* context) {
    SaflokApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventViewExit);
}

void saflok_scene_save_success_on_enter(void* context) {
    SaflokApp* app = context;
 
    FURI_LOG_D(TAG, "on_enter");

    Popup* popup = app->popup;
    popup_set_icon(popup, 36, 5, &I_DolphinSaved_92x58);
    popup_set_header(popup, "Saved", 15, 19, AlignLeft, AlignBottom);
    popup_set_timeout(popup, 1500);
    popup_set_context(popup, app);
    popup_set_callback(popup, saflok_scene_save_success_popup_callback);
    popup_enable_timeout(popup);
    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewPopup);
}

bool saflok_scene_save_success_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    
    FURI_LOG_D(TAG, "on_event");
    
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SaflokCustomEventViewExit) {
            if(scene_manager_has_previous_scene(app->scene_manager, SaflokSceneMfClassicKeys)) {
                consumed = scene_manager_search_and_switch_to_previous_scene(
                    app->scene_manager, SaflokSceneMfClassicKeys);
            } else if(scene_manager_has_previous_scene(app->scene_manager, SaflokSceneSaveConfirm)) {
                scene_manager_next_scene(app->scene_manager, SaflokSceneMfClassicDetectReader);
                consumed = true;
            } else if(scene_manager_has_previous_scene(app->scene_manager, SaflokSceneReadSuccess)) {
                consumed = scene_manager_search_and_switch_to_another_scene(
                    app->scene_manager, SaflokSceneFileSelect);
            } else if(scene_manager_has_previous_scene(app->scene_manager, SaflokSceneFileSelect)) {
                consumed = scene_manager_search_and_switch_to_previous_scene(
                    app->scene_manager, SaflokSceneFileSelect);
            } else if(scene_manager_has_previous_scene(app->scene_manager, SaflokSceneStart)) {
                consumed = scene_manager_search_and_switch_to_previous_scene(
                    app->scene_manager, SaflokSceneStart);
            } else {
                consumed = scene_manager_search_and_switch_to_another_scene(
                    app->scene_manager, SaflokSceneFileSelect);
            }
        }
    }

    return consumed;
}

void saflok_scene_save_success_on_exit(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_exit");
    
    popup_reset(app->popup);
}
