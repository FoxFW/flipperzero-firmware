#include "../saflok_app_i.h"
#include <furi.h>
#include <core/log.h>
#include <core/string.h>
#include <furi_hal.h>

void saflok_scene_dict_attack_submenu_callback(void* context, uint32_t index) {
    SaflokApp* app = context;

    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void saflok_scene_dict_attack_on_enter(void* context) {
    UNUSED(context);
}

bool saflok_scene_dict_attack_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);

    return false;
}

void saflok_scene_dict_attack_on_exit(void* context) {
    UNUSED(context);
}

