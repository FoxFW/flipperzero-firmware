#include "../saflok_app_i.h"

#undef TAG
#define TAG "SaflokSceneMfClassicKeys"

#define SAFLOK_SCENE_MF_CLASSIC_KEYS_MAX (100)

void saflok_scene_mf_classic_keys_widget_callback(GuiButtonType result, InputType type, void* context) {
    SaflokApp* app = context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, result);
    }
}

void saflok_scene_mf_classic_keys_on_enter(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_enter");

    uint32_t flipper_dict_keys_total = 0;
    KeysDict* dict = keys_dict_alloc(
        SAFLOK_APP_MF_CLASSIC_DICT_SYSTEM_PATH, KeysDictModeOpenExisting, sizeof(MfClassicKey));
    flipper_dict_keys_total = keys_dict_get_total_keys(dict);
    keys_dict_free(dict);

    uint32_t user_dict_keys_total = 0;
    dict = keys_dict_alloc(
        SAFLOK_APP_MF_CLASSIC_DICT_USER_PATH, KeysDictModeOpenAlways, sizeof(MfClassicKey));
    user_dict_keys_total = keys_dict_get_total_keys(dict);
    keys_dict_free(dict);

    FuriString* temp_str = furi_string_alloc();
    widget_add_string_element(
        app->widget, 0, 0, AlignLeft, AlignTop, FontPrimary, "MIFARE Classic Keys");
    furi_string_printf(temp_str, "System dict: %lu", flipper_dict_keys_total);
    widget_add_string_element(
        app->widget,
        0,
        20,
        AlignLeft,
        AlignTop,
        FontSecondary,
        furi_string_get_cstr(temp_str));
    furi_string_printf(temp_str, "User dict: %lu", user_dict_keys_total);
    widget_add_string_element(
        app->widget,
        0,
        32,
        AlignLeft,
        AlignTop,
        FontSecondary,
        furi_string_get_cstr(temp_str));
    widget_add_icon_element(app->widget, 87, 13, &I_Keychain_39x36);
    widget_add_button_element(
        app->widget,
        GuiButtonTypeCenter,
        "Add",
        saflok_scene_mf_classic_keys_widget_callback,
        app);
    if(user_dict_keys_total > 0) {
        widget_add_button_element(
            app->widget,
            GuiButtonTypeRight,
            "List",
            saflok_scene_mf_classic_keys_widget_callback,
            app);
    }
    furi_string_free(temp_str);

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewWidget);
}

bool saflok_scene_mf_classic_keys_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    
    FURI_LOG_D(TAG, "on_event");
    
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == GuiButtonTypeCenter) {
            scene_manager_next_scene(app->scene_manager, SaflokSceneMfClassicKeysAdd);
            consumed = true;
        } else if(event.event == GuiButtonTypeRight) {
            scene_manager_next_scene(app->scene_manager, SaflokSceneMfClassicKeysList);
            consumed = true;
        }
    }

    return consumed;
}

void saflok_scene_mf_classic_keys_on_exit(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_exit");
    
    widget_reset(app->widget);
}
