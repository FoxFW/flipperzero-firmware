#include "../saflok_app_i.h"
#include "views/field_list.h"
#include <gui/canvas.h>
#include <furi.h>
#include <core/log.h>
#include <gui/view.h>
#include <gui/elements.h>

static void field_select_callback(void* context, uint8_t index) {
    SaflokApp* app = context;
    
    app->previous_field_index = index;
    
    switch(index) {
        case 0:
            scene_manager_next_scene(app->scene_manager, SaflokSceneKeyLevel);
            break;
        case 1:
            scene_manager_next_scene(app->scene_manager, SaflokSceneSubFunction);
            break;
        case 2:
            scene_manager_next_scene(app->scene_manager, SaflokSceneKeyID);
            break;
        case 3:
            scene_manager_next_scene(app->scene_manager, SaflokSceneLedWarnings);
            break;
        case 4:
            scene_manager_next_scene(app->scene_manager, SaflokSceneKeyRecord);
            break;
        case 5:
            scene_manager_next_scene(app->scene_manager, SaflokScenePassLevels);
            break;
        case 6:
            scene_manager_next_scene(app->scene_manager, SaflokSceneOpeningKey);
            break;
        case 7:
            scene_manager_next_scene(app->scene_manager, SaflokSceneSequenceCombo);
            break;
        case 8:
            scene_manager_next_scene(app->scene_manager, SaflokSceneOverrideDeadbolt);
            break;
        case 9:
            scene_manager_next_scene(app->scene_manager, SaflokSceneRestrictions);
            break;
        case 10:
            scene_manager_next_scene(app->scene_manager, SaflokSceneCreationDate);
            break;
        case 11:
            scene_manager_next_scene(app->scene_manager, SaflokSceneExpirationDate);
            break;
        case 12:
            scene_manager_next_scene(app->scene_manager, SaflokScenePropertyNumber);
            break;
    }
}

void saflok_scene_field_select_on_enter(void* context) {
    SaflokApp* app = context;
    
    field_list_reset(app->field_list);
    field_list_set_callback(app->field_list, field_select_callback, app);
    field_list_set_saflok_data(app->field_list, app->saflok_data);
    field_list_set_selected_item(app->field_list, app->previous_field_index);
    
    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewFieldList);
}

bool saflok_scene_field_select_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeBack) {
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, SaflokSceneTopLevel);
        consumed = true;
    }

    return consumed;
}

void saflok_scene_field_select_on_exit(void* context) {
    UNUSED(context);
}
