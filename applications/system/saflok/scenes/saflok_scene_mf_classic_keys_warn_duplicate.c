#include "../saflok_app_i.h"

#undef TAG
#define TAG "SaflokSceneMfClassicKeysWarnDuplicate"

void saflok_scene_mf_classic_keys_warn_duplicate_popup_callback(void* context) {
    SaflokApp* app = context;

    view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventViewExit);
}

void saflok_scene_mf_classic_keys_warn_duplicate_on_enter(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_enter");

    Popup* popup = app->popup;
    popup_set_icon(popup, 83, 22, &I_WarningDolphinFlip_45x42);
    popup_set_header(popup, "Key Already Exists!", 64, 3, AlignCenter, AlignTop);
    popup_set_text(
        popup,
        "Please enter a\n"
        "different key.",
        4,
        24,
        AlignLeft,
        AlignTop);
    popup_set_timeout(popup, 1500);
    popup_set_context(popup, app);
    popup_set_callback(popup, saflok_scene_mf_classic_keys_warn_duplicate_popup_callback);
    popup_enable_timeout(popup);
    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewPopup);
}

bool saflok_scene_mf_classic_keys_warn_duplicate_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    
    FURI_LOG_D(TAG, "on_event");
    
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SaflokCustomEventViewExit) {
            consumed = scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, SaflokSceneMfClassicKeysAdd);
        }
    }

    return consumed;
}

void saflok_scene_mf_classic_keys_warn_duplicate_on_exit(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_exit");
    
    popup_reset(app->popup);
}
