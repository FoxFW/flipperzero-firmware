#include "../saflok_app_i.h"

#undef TAG
#define TAG "SaflokSceneRetryConfirm"

void saflok_scene_retry_confirm_dialog_callback(DialogExResult result, void* context) {
    SaflokApp* app = context;

    view_dispatcher_send_custom_event(app->view_dispatcher, result);
}

void saflok_scene_retry_confirm_on_enter(void* context) {
    SaflokApp* app = context;
    
    FURI_LOG_D(TAG, "on_enter");

    DialogEx* dialog_ex = app->dialog_ex;

    dialog_ex_set_left_button_text(dialog_ex, "Retry");
    dialog_ex_set_right_button_text(dialog_ex, "Stay");
    dialog_ex_set_header(dialog_ex, "Retry Reading?", 64, 0, AlignCenter, AlignTop);
    dialog_ex_set_text(dialog_ex, "All unsaved data will be lost", 64, 12, AlignCenter, AlignTop);
    dialog_ex_set_context(dialog_ex, app);
    dialog_ex_set_result_callback(dialog_ex, saflok_scene_retry_confirm_dialog_callback);

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewDialogEx);
}

bool saflok_scene_retry_confirm_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    
    FURI_LOG_D(TAG, "on_event");
    
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == DialogExResultRight) {
            consumed = scene_manager_previous_scene(app->scene_manager);
        } else if(event.event == DialogExResultLeft) {
            if(scene_manager_has_previous_scene(app->scene_manager, SaflokSceneMfClassicDictAttack)) {
                consumed = scene_manager_search_and_switch_to_previous_scene(
                    app->scene_manager, SaflokSceneMfClassicDictAttack);
            } else if(scene_manager_has_previous_scene(app->scene_manager, SaflokSceneDetect)) {
                consumed = scene_manager_search_and_switch_to_previous_scene(
                    app->scene_manager, SaflokSceneDetect);
            } else if(scene_manager_has_previous_scene(app->scene_manager, SaflokSceneRead)) {
                consumed = scene_manager_search_and_switch_to_previous_scene(
                    app->scene_manager, SaflokSceneRead);
            }
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        consumed = true;
    }

    return consumed;
}

void saflok_scene_retry_confirm_on_exit(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_exit");
    
    dialog_ex_reset(app->dialog_ex);
}
