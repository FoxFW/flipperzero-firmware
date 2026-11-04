#include "../keeloq_tool_i.h"

#define KEELOQ_TOOL_ERROR_EVENT 0xEEu

static void keeloq_tool_scene_error_callback(void* context) {
    KeeloqTool* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, KEELOQ_TOOL_ERROR_EVENT);
}

void keeloq_tool_scene_error_on_enter(void* context) {
    KeeloqTool* app = context;
    popup_reset(app->popup);
    popup_set_header(app->popup, "Error", 64, 11, AlignCenter, AlignTop);
    popup_set_text(
        app->popup, furi_string_get_cstr(app->error_str), 64, 26, AlignCenter, AlignTop);
    popup_set_context(app->popup, app);
    popup_set_callback(app->popup, keeloq_tool_scene_error_callback);
    popup_set_timeout(app->popup, 1500);
    popup_enable_timeout(app->popup);
    view_dispatcher_switch_to_view(app->view_dispatcher, KeeloqToolViewPopup);
}

bool keeloq_tool_scene_error_on_event(void* context, SceneManagerEvent event) {
    KeeloqTool* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == KEELOQ_TOOL_ERROR_EVENT) {
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    return false;
}

void keeloq_tool_scene_error_on_exit(void* context) {
    KeeloqTool* app = context;
    popup_reset(app->popup);
}
