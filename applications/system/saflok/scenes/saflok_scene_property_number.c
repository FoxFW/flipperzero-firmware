#include "../saflok_app_i.h"
#include "../helpers/saflok_app_helpers.h"
#include "../helpers/saflok_field_helpers.h"

static void property_number_input_callback(void* context, int32_t value) {
    SaflokApp* app = context;
    
    if (app->saflok_data->identification.property_number != (uint16_t)value) {
        app->saflok_data->identification.property_number = value;
        
        app->changed->property_number = true;
    }
    
    scene_manager_previous_scene(app->scene_manager);
}

void saflok_scene_property_number_on_enter(void* context) {
    SaflokApp* app = context;
    

    if(app->display_settings->property_number_hex_format) {
        hex_input_set_header_text(app->hex_input, "Enter Property Number");
        hex_input_set_result_callback(app->hex_input, property_number_input_callback, app, app->saflok_data->identification.property_number, 0, 9999);
        view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewHexInput);
    } else {
        number_input_set_header_text(app->number_input, "Enter Property Number");
        number_input_set_result_callback(app->number_input, property_number_input_callback, app, app->saflok_data->identification.property_number, 0, 9999);
        view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewNumberInput);
    }
}

bool saflok_scene_property_number_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeBack) {
        consumed = scene_manager_previous_scene(app->scene_manager);
    }

    return consumed;
}

void saflok_scene_property_number_on_exit(void* context) {
    SaflokApp* app = context;

    if(app->changed->property_number) {
        saflok_pack_current_property_number(app);
    }
}
