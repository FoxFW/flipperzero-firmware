#include "../saflok_app_i.h"
#include "views/aligned_text_box.h"
#include <gui/modules/text_box.h>
#include <gui/canvas.h>
#include <gui/modules/submenu.h>

#define FIELD_COUNT  14
#define TAG          "SaflokSceneViewInfo"

extern void submenu_callback(void* context, uint32_t index);

void saflok_scene_view_info_on_enter(void* context) {
    SaflokApp* app = context;
    furi_assert(app);
    furi_assert(app->aligned_text_box);

    aligned_text_box_reset(app->aligned_text_box);
    aligned_text_box_set_saflok_data(app->aligned_text_box, app->saflok_data);
    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewAlignedTextBox);
}

bool saflok_scene_view_info_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    bool consumed = false;
    
    furi_assert(app);
    
    FURI_LOG_D("ViewInfo", "Handling event type: %d", event.type);
    
    if(event.type == SceneManagerEventTypeBack) {
        FURI_LOG_D("ViewInfo", "Back event received");
        
        if(!app->view_dispatcher) {
            FURI_LOG_E("ViewInfo", "view_dispatcher is NULL!");
            return false;
        }
        
        if(!app->topmenu) {
            FURI_LOG_E("ViewInfo", "topmenu is NULL!");
            return false;
        }
        
        FURI_LOG_D("ViewInfo", "Switching to TopMenuView");
        view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewTopMenu);
        view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventReturnFromViewInfo);
        
        FURI_LOG_D("ViewInfo", "Going to previous scene");
        return scene_manager_previous_scene(app->scene_manager);
    }

    return consumed;
}

void saflok_scene_view_info_on_exit(void* context) {
    SaflokApp* app = context;
    furi_assert(app);
    
    if(app->aligned_text_box) {
        aligned_text_box_reset(app->aligned_text_box);
    }
}
