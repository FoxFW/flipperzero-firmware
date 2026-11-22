#include "../saflok_app_i.h"

#undef TAG
#define TAG "SaflokSceneField"

void saflok_scene_field_on_enter(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_enter");

    furi_hal_nfc_low_power_mode_stop();
    furi_hal_nfc_poller_field_on();
    Popup* popup = app->popup;
    popup_set_header(
        popup,
        "Field is on\nDon't leave device\nin this mode for too long.",
        64,
        11,
        AlignCenter,
        AlignTop);
    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewPopup);

    notification_internal_message(app->notifications, &sequence_set_blue_255);
}

bool saflok_scene_field_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    
    FURI_LOG_D(TAG, "on_event");
    
    return false;
}

void saflok_scene_field_on_exit(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_exit");
    
    furi_hal_nfc_low_power_mode_start();
    notification_internal_message(app->notifications, &sequence_reset_blue);
    popup_reset(app->popup);
}
