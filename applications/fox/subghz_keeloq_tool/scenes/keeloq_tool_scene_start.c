#include "../keeloq_tool_i.h"

typedef enum {
    KeeloqToolStartKeys,
    KeeloqToolStartBf2,
} KeeloqToolStartIndex;

static void keeloq_tool_scene_start_submenu_callback(void* context, uint32_t index) {
    KeeloqTool* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void keeloq_tool_scene_start_on_enter(void* context) {
    KeeloqTool* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "KeeLoq Tool");
    submenu_add_item(
        app->submenu, "Manage Keys", KeeloqToolStartKeys,
        keeloq_tool_scene_start_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Bruteforce (2 captures)", KeeloqToolStartBf2,
        keeloq_tool_scene_start_submenu_callback, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, KeeloqToolViewSubmenu);
}

bool keeloq_tool_scene_start_on_event(void* context, SceneManagerEvent event) {
    KeeloqTool* app = context;
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == KeeloqToolStartKeys) {
            scene_manager_next_scene(app->scene_manager, KeeloqToolSceneKeys);
            return true;
        } else if(event.event == KeeloqToolStartBf2) {
            scene_manager_set_scene_state(app->scene_manager, KeeloqToolSceneBf2, 0);
            scene_manager_next_scene(app->scene_manager, KeeloqToolSceneBf2);
            return true;
        }
    }
    return false;
}

void keeloq_tool_scene_start_on_exit(void* context) {
    KeeloqTool* app = context;
    submenu_reset(app->submenu);
}
