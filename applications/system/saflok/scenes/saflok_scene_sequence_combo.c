#include "../saflok_app_i.h"
#include "../helpers/saflok_app_helpers.h"
#include "../helpers/saflok_field_helpers.h"

static void sequence_combo_input_callback(void* context, int32_t value) {
    SaflokApp* app = context;
    
    uint16_t new_sequence = (uint16_t)(value & 0x0FFF);

    if(app->saflok_data->security.sequence_combination != new_sequence) {
        app->saflok_data->security.sequence_combination = new_sequence;
        app->changed->sequence_combo = true;
    }
    
    scene_manager_previous_scene(app->scene_manager);
}

void saflok_scene_sequence_combo_on_enter(void* context) {
    SaflokApp* app = context;
    
    if(app->display_settings->sequence_combo_hex_format) {
        hex_input_set_header_text(app->hex_input, "Enter Sequence & Combination");
        hex_input_set_result_callback(app->hex_input, sequence_combo_input_callback, app, app->saflok_data->security.sequence_combination, 0, 9999);
        view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewHexInput);
    } else {
        number_input_set_header_text(app->number_input, "Enter Sequence & Combination");
        number_input_set_result_callback(app->number_input, sequence_combo_input_callback, app, app->saflok_data->security.sequence_combination, 0, 9999);
        view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewNumberInput);
    }
}

bool saflok_scene_sequence_combo_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeBack) {
        consumed = scene_manager_previous_scene(app->scene_manager);
    }
    return consumed;
}

void saflok_scene_sequence_combo_on_exit(void* context) {
    SaflokApp* app = context;
   
    if(app->changed->sequence_combo) {
        saflok_pack_current_sequence_combo(app);
    }
}

