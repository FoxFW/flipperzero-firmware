#include "../saflok_app_i.h"
#include "../helpers/saflok_app_helpers.h"
#include "../helpers/saflok_field_helpers.h"
#include <gui/modules/variable_item_list.h>
#include <gui/view_dispatcher.h>

#define KEY_LEVEL_OPTIONS_COUNT (sizeof(key_levels) / sizeof(key_levels[0]))

static void key_level_enter_callback(void* context, uint32_t index) {
    SaflokApp* app = context;

    if(app->saflok_data->access_control.key_level.value != key_levels[index].value) {
        app->saflok_data->access_control.key_level = key_levels[index];
        app->changed->key_level = true;
    }
    
    scene_manager_previous_scene(app->scene_manager);
}

void saflok_scene_key_level_on_enter(void* context) {
    SaflokApp* app = context;
    variable_item_list_reset(app->key_level_list);

    variable_item_list_set_header(app->key_level_list, "Select Key Level");

    for(uint32_t i = 0; i < KEY_LEVEL_OPTIONS_COUNT; i++) {
        variable_item_list_add(
            app->key_level_list,
            key_levels[i].name,
            0,
            NULL,
            NULL);
    }

    uint8_t current_level_index = app->saflok_data->access_control.key_level.value - 1; 
    if(current_level_index >= KEY_LEVEL_OPTIONS_COUNT) {
        current_level_index = 0;
    }
    variable_item_list_set_selected_item(app->key_level_list, current_level_index);

    variable_item_list_set_enter_callback(
        app->key_level_list,
        key_level_enter_callback,
        app);

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewKeyLevel);
}

bool saflok_scene_key_level_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeBack) {
        consumed = scene_manager_previous_scene(app->scene_manager);
    }

    return consumed;
}

void saflok_scene_key_level_on_exit(void* context) {
    SaflokApp* app = context;
    
    if(app->changed->key_level) {
        saflok_pack_current_key_level(app);
    }

    variable_item_list_reset(app->key_level_list);
}

