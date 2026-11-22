#include "../saflok_app_i.h"

#undef TAG
#define TAG "SaflokSceneFileSelect"

void saflok_scene_file_select_on_enter(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_enter");

    if(saflok_load_from_file_select(app)) {
        scene_manager_next_scene(app->scene_manager, SaflokSceneTopLevel);
    } else {
        scene_manager_previous_scene(app->scene_manager);
    }
}

bool saflok_scene_file_select_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    
    FURI_LOG_D(TAG, "on_event");
    
    bool consumed = false;
    return consumed;
}

void saflok_scene_file_select_on_exit(void* context) {
    FURI_LOG_D(TAG, "on_exit");
    UNUSED(context);
}
