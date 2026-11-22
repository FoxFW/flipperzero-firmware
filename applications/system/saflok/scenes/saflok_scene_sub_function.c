#include <gui/modules/variable_item_list.h>
#include <gui/view_dispatcher.h>
#include "../saflok_app_i.h"
#include "../helpers/saflok_app_helpers.h"
#include "../helpers/saflok_field_helpers.h"

#undef TAG
#define TAG "SaflokSceneSubFunction"

#define SUB_FUNCTION_OPTIONS_COUNT (sizeof(sub_functions) / sizeof(sub_functions[0]))

static void sub_function_enter_callback(void* context, uint32_t index) {
    SaflokApp* app = context;

    if (app->saflok_data->access_control.sub_function.value != sub_functions[index].value) {
        app->saflok_data->access_control.sub_function = sub_functions[index];
        app->changed->sub_function = true;
    }

    scene_manager_previous_scene(app->scene_manager);
}

void saflok_scene_sub_function_on_enter(void* context) {
    SaflokApp* app = context;
    variable_item_list_reset(app->sub_function_list);

    variable_item_list_set_header(app->sub_function_list, "Select Sub Function");

    for(uint32_t i = 0; i < SUB_FUNCTION_OPTIONS_COUNT; i++) {
        variable_item_list_add(
            app->sub_function_list,
            sub_functions[i].name,
            0,
            NULL,
            NULL);
    }

    uint8_t current_function_index = app->saflok_data->access_control.sub_function.value;
    if(current_function_index >= SUB_FUNCTION_OPTIONS_COUNT) {
        current_function_index = 0;
    }
    variable_item_list_set_selected_item(app->sub_function_list, current_function_index);

    variable_item_list_set_enter_callback(
        app->sub_function_list,
        sub_function_enter_callback,
        app);

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewSubFunction);
}

bool saflok_scene_sub_function_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeBack) {
        consumed = scene_manager_previous_scene(app->scene_manager);
    }

    return consumed;
}

void saflok_scene_sub_function_on_exit(void* context) {
    SaflokApp* app = context;

    if(app->changed->sub_function) {
        saflok_pack_current_sub_function(app);
    }

    variable_item_list_reset(app->sub_function_list);
}

