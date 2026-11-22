#include "../saflok_app_i.h"
#include "../helpers/saflok_app_helpers.h"
#include "../helpers/saflok_field_helpers.h"

void override_deadbolt_update(SaflokApp* app, bool new_value) {
    if(app->saflok_data->access_control.override_deadbolt != new_value) {
        app->saflok_data->access_control.override_deadbolt = new_value;
        app->changed->override_deadbolt = true;
    }
    scene_manager_previous_scene(app->scene_manager);
}

static void dialog_ex_callback(DialogExResult result, void* context) {
    SaflokApp* app = context;
    
    switch(result) {
        case DialogExResultLeft:
            override_deadbolt_update(app, false);
            break;
        case DialogExResultRight:
            override_deadbolt_update(app, true);
            break;
        default:
            break;
    }
}

void saflok_scene_override_deadbolt_on_enter(void* context) {
    SaflokApp* app = context;
    DialogEx* dialog_ex = app->dialog_ex;
    
    dialog_ex_set_header(dialog_ex, "Override Deadbolt", 64, 10, AlignCenter, AlignTop);
    dialog_ex_set_text(dialog_ex, "Allow this key to\noverride deadbolt?", 64, 32, AlignCenter, AlignCenter);
    
    const char* current_value = app->saflok_data->access_control.override_deadbolt ? 
        "Currently: Yes" : "Currently: No";
    dialog_ex_set_text(dialog_ex, current_value, 64, 46, AlignCenter, AlignCenter);
    
    dialog_ex_set_left_button_text(dialog_ex, "No");
    dialog_ex_set_right_button_text(dialog_ex, "Yes");
    
    dialog_ex_set_context(dialog_ex, app);
    dialog_ex_set_result_callback(dialog_ex, dialog_ex_callback);
    
    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewDialogEx);
}

bool saflok_scene_override_deadbolt_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    bool consumed = false;
    
    if(event.type == SceneManagerEventTypeBack) {
        consumed = scene_manager_previous_scene(app->scene_manager);
    }
    
    return consumed;
}

void saflok_scene_override_deadbolt_on_exit(void* context) {
    SaflokApp* app = context;
    
    if(app->changed->override_deadbolt || 
       app->saflok_data->access_control.override_deadbolt != saflok_packed_override_deadbolt(app)) {
        
        saflok_pack_current_override_deadbolt(app);
    }
   
    dialog_ex_reset(app->dialog_ex);
}

