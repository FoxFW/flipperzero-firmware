#include "../saflok_app_i.h"

#undef TAG
#define TAG "SaflokSceneMfClassicMfkeyComplete"

void saflok_scene_mf_classic_mfkey_complete_callback(
    GuiButtonType result,
    InputType type,
    void* context) {
    SaflokApp* app = context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, result);
    }
}

void saflok_scene_mf_classic_mfkey_complete_on_enter(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_enter");

    widget_add_string_element(app->widget, 64, 2, AlignCenter, AlignTop, FontPrimary, "Completed!");
    widget_add_string_multiline_element(
        app->widget,
        64,
        16,
        AlignCenter,
        AlignTop,
        FontSecondary,
        "Now use Mfkey32 to extract \nkeys: r.flipper.net/nfc-tools");
    widget_add_string_element(
        app->widget, 29, 38, AlignLeft, AlignTop, FontSecondary, "or Apps > NFC > MFKey");
    widget_add_icon_element(app->widget, 0, 39, &I_MFKey_qr_25x25);
    widget_add_button_element(
        app->widget,
        GuiButtonTypeRight,
        "Finish",
        saflok_scene_mf_classic_mfkey_complete_callback,
        app);

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewWidget);
}

bool saflok_scene_mf_classic_mfkey_complete_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    
    FURI_LOG_D(TAG, "on_event");
    
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == GuiButtonTypeRight) {
            consumed = scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, SaflokSceneStart);
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        const uint32_t prev_scenes[] = {SaflokSceneStart};
        consumed = scene_manager_search_and_switch_to_previous_scene_one_of(
            app->scene_manager, prev_scenes, COUNT_OF(prev_scenes));
    }

    return consumed;
}

void saflok_scene_mf_classic_mfkey_complete_on_exit(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_exit");
    
    widget_reset(app->widget);
}
