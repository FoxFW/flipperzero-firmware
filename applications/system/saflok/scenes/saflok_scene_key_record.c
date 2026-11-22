#include "../saflok_app_i.h"
#include "../helpers/saflok_app_helpers.h"
#include "../helpers/saflok_field_helpers.h"

static void key_record_input_callback(void* context, int32_t value) {
    SaflokApp* app = context;
    
    uint16_t new_key_record = value & 0x7FFF;
    
    if(app->saflok_data->identification.key_record != new_key_record) {
        app->saflok_data->identification.key_record = new_key_record;
    
        app->changed->key_record = true;
    }
    
    scene_manager_previous_scene(app->scene_manager);
}

void saflok_scene_key_record_on_enter(void* context) {
    SaflokApp* app = context;
   
    if(app->display_settings->key_record_hex_format) {
        hex_input_set_header_text(app->hex_input, "Enter Key Record");
        hex_input_set_result_callback(app->hex_input, key_record_input_callback, app, app->saflok_data->identification.key_record, 0, 0x7FFF);
        view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewHexInput);
    } else {
        number_input_set_header_text(app->number_input, "Enter Key Record");
        number_input_set_result_callback(app->number_input, key_record_input_callback, app, app->saflok_data->identification.key_record, 0, 0x7FFF);
        view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewNumberInput);
    }
}

bool saflok_scene_key_record_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeBack) {
        consumed = scene_manager_previous_scene(app->scene_manager);
    }
    return consumed;
}

void saflok_scene_key_record_on_exit(void* context) {
    SaflokApp* app = context;

    if(app->changed->key_record) {
        saflok_pack_current_key_record(app);
    }
}
