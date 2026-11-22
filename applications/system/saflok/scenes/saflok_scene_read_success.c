#include "../saflok_app_i.h"

#include "../helpers/protocol_support/mf_classic/mf_classic_render.h"
#include "../helpers/protocol_support/saflok_protocol_support_defs.h"
#include "../helpers/protocol_support/saflok_protocol_support_gui_common.h"
#include "../helpers/saflok_field_helpers.h"
#include "../helpers/saflok_app_helpers.h"
#include "../helpers/saflok_encryption_helpers.h"

#undef TAG
#define TAG "SaflokSceneReadSuccess"

void saflok_scene_read_success_on_enter(void* context) {
    FURI_LOG_D(TAG, "on_enter");
    furi_assert(context);

    SaflokApp* app = context;

        scene_manager_next_scene(app->scene_manager, SaflokSceneTopLevel);






}

bool saflok_scene_read_success_on_event(void* context, SceneManagerEvent event) {
    UNUSED(event);
    FURI_LOG_D(TAG, "on_event");
    furi_assert(context);
    bool consumed = true;



    return consumed;
}

void saflok_scene_read_success_on_exit(void* context) {
    FURI_LOG_D(TAG, "on_exit");
    furi_assert(context);

    SaflokApp* app = context;
    notification_message_block(app->notifications, &sequence_reset_green);
    widget_reset(app->widget);
}
