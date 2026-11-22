#include "../saflok_app_i.h"

#undef TAG
#define TAG "SaflokSceneMfClassicKeysDelete"

void saflok_scene_mf_classic_keys_delete_widget_callback(
    GuiButtonType result,
    InputType type,
    void* context) {
    SaflokApp* app = context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, result);
    }
}

void saflok_scene_mf_classic_keys_delete_on_enter(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_enter");

    uint32_t key_index = scene_manager_get_scene_state(app->scene_manager, SaflokSceneMfClassicKeysDelete);
    FuriString* key_str = furi_string_alloc();

    widget_add_string_element(
        app->widget, 64, 0, AlignCenter, AlignTop, FontPrimary, "Delete this key?");
    widget_add_button_element(
        app->widget,
        GuiButtonTypeLeft,
        "Cancel",
        saflok_scene_mf_classic_keys_delete_widget_callback,
        app);
    widget_add_button_element(
        app->widget,
        GuiButtonTypeRight,
        "Delete",
        saflok_scene_mf_classic_keys_delete_widget_callback,
        app);

    mf_user_dict_get_key_str(app->mf_user_dict, key_index, key_str);
    widget_add_string_element(
        app->widget,
        64,
        32,
        AlignCenter,
        AlignCenter,
        FontSecondary,
        furi_string_get_cstr(key_str));

    furi_string_free(key_str);

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewWidget);
}

bool saflok_scene_mf_classic_keys_delete_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    
    FURI_LOG_D(TAG, "on_event");
    
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == GuiButtonTypeRight) {
            uint32_t key_index = scene_manager_get_scene_state(
                app->scene_manager, SaflokSceneMfClassicKeysDelete);
            if(mf_user_dict_delete_key(app->mf_user_dict, key_index)) {
                scene_manager_next_scene(app->scene_manager, SaflokSceneDeleteSuccess);
            } else {
                scene_manager_previous_scene(app->scene_manager);
            }
        } else if(event.event == GuiButtonTypeLeft) {
            scene_manager_previous_scene(app->scene_manager);
        }
        consumed = true;
    }

    return consumed;
}

void saflok_scene_mf_classic_keys_delete_on_exit(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_exit");
    
    mf_user_dict_free(app->mf_user_dict);
    widget_reset(app->widget);
}
