#include "../saflok_app_i.h"

#undef TAG
#define TAG "SaflokSceneSaveConfirm"

void saflok_scene_save_confirm_dialog_callback(DialogExResult result, void* context) {
    SaflokApp* app = context;

    view_dispatcher_send_custom_event(app->view_dispatcher, result);
}

void saflok_scene_save_confirm_on_enter(void* context) {
    SaflokApp* app = context;
    
    FURI_LOG_D(TAG, "on_enter");

    DialogEx* dialog_ex = app->dialog_ex;

    dialog_ex_set_left_button_text(dialog_ex, "Skip");
    dialog_ex_set_right_button_text(dialog_ex, "Save");
    dialog_ex_set_header(dialog_ex, "Save the Key?", 64, 0, AlignCenter, AlignTop);
    dialog_ex_set_text(dialog_ex, "All unsaved data will be lost", 64, 12, AlignCenter, AlignTop);
    dialog_ex_set_context(dialog_ex, app);
    dialog_ex_set_result_callback(dialog_ex, saflok_scene_save_confirm_dialog_callback);

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewDialogEx);
}

bool saflok_scene_save_confirm_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    
    FURI_LOG_D(TAG, "on_event");
    
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == DialogExResultRight) {
            scene_manager_next_scene(app->scene_manager, SaflokSceneSaveName);
            consumed = true;
        } else if(event.event == DialogExResultLeft) {
            scene_manager_next_scene(app->scene_manager, SaflokSceneMfClassicDetectReader);
            consumed = true;
        }
    }
    return consumed;
}

void saflok_scene_save_confirm_on_exit(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_exit");
    
    dialog_ex_reset(app->dialog_ex);
}
