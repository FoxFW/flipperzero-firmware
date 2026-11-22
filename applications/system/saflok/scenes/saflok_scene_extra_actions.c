#include "../saflok_app_i.h"

#undef TAG
#define TAG "SaflokSceneExtraActions"

enum SubmenuIndex {
    SubmenuIndexReadCardType,
    SubmenuIndexMfClassicKeys,
};

void saflok_scene_extra_actions_submenu_callback(void* context, uint32_t index) {
    SaflokApp* app = context;

    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void saflok_scene_extra_actions_on_enter(void* context) {
    SaflokApp* app = context;
    
    FURI_LOG_D(TAG, "on_enter");

    Submenu* submenu = app->submenu;

    submenu_add_item(
        submenu,
        "Read Specific Card Type",
        SubmenuIndexReadCardType,
        saflok_scene_extra_actions_submenu_callback,
        app);
    submenu_add_item(
        submenu,
        "MIFARE Classic Keys",
        SubmenuIndexMfClassicKeys,
        saflok_scene_extra_actions_submenu_callback,
        app);
    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, SaflokSceneExtraActions));
    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewMenu);
}

bool saflok_scene_extra_actions_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    
    FURI_LOG_D(TAG, "on_event");
    
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubmenuIndexMfClassicKeys) {
            scene_manager_next_scene(app->scene_manager, SaflokSceneMfClassicKeys);
            consumed = true;
        } else if(event.event == SubmenuIndexReadCardType) {
            scene_manager_next_scene(app->scene_manager, SaflokSceneSelectProtocol);
            consumed = true;
        }
        scene_manager_set_scene_state(app->scene_manager, SaflokSceneExtraActions, event.event);
    }

    return consumed;
}

void saflok_scene_extra_actions_on_exit(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_exit");
    
    submenu_reset(app->submenu);
}
