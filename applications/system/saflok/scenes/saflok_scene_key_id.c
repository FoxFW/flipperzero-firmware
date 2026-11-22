#include "../saflok_app_i.h"
#include "../helpers/saflok_app_helpers.h"
#include "../helpers/saflok_field_helpers.h"

static void key_id_input_callback(void* context, int32_t value) {
    SaflokApp* app = context;
    
    uint8_t new_key_id = (uint8_t)(value & 0xFF);

    if(app->saflok_data->identification.key_id != new_key_id) {
        app->saflok_data->identification.key_id = new_key_id;
        app->changed->key_id = true;
    }
    
    scene_manager_previous_scene(app->scene_manager);
}

void saflok_scene_key_id_on_enter(void* context) {
    SaflokApp* app = context;
    
    if(app->display_settings->key_id_hex_format) {
        hex_input_set_header_text(app->hex_input, "Enter Key ID");
        hex_input_set_result_callback(app->hex_input, key_id_input_callback, app, app->saflok_data->identification.key_id, 0, 255);
        view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewHexInput);
    } else {
        number_input_set_header_text(app->number_input, "Enter Key ID");
        number_input_set_result_callback(app->number_input, key_id_input_callback, app, app->saflok_data->identification.key_id, 0, 255);
        view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewNumberInput);
    }
}

bool saflok_scene_key_id_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    bool consumed = false;
    
    if(event.type == SceneManagerEventTypeBack) {
        consumed = scene_manager_previous_scene(app->scene_manager);
    }
    
    return consumed;
}

void saflok_scene_key_id_on_exit(void* context) {
    SaflokApp* app = context;

    if(app->changed->key_id) {
        saflok_pack_current_key_id(app);
    }
}

