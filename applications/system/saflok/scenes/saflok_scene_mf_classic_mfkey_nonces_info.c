#include "../saflok_app_i.h"

#undef TAG
#define TAG "SaflokSceneMfClassicMfkeyNoncesInfo"

void saflok_scene_mf_classic_mfkey_nonces_info_callback(
    GuiButtonType result,
    InputType type,
    void* context) {
    SaflokApp* app = context;

    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, result);
    }
}

void saflok_scene_mf_classic_mfkey_nonces_info_on_enter(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_enter");

    FuriString* temp_str = furi_string_alloc();

    size_t mfkey_params_saved = mfkey32_logger_get_params_num(app->mfkey32_logger);
    furi_string_printf(temp_str, "Nonce pairs saved: %zu\n", mfkey_params_saved);
    widget_add_string_element(
        app->widget, 0, 0, AlignLeft, AlignTop, FontPrimary, furi_string_get_cstr(temp_str));
    widget_add_string_element(
        app->widget, 0, 12, AlignLeft, AlignTop, FontSecondary, "Authenticated sectors:");

    mfkey32_logger_get_params_data(app->mfkey32_logger, temp_str);
    widget_add_text_scroll_element(
        app->widget, 0, 22, 128, 42, furi_string_get_cstr(temp_str));
    widget_add_button_element(
        app->widget,
        GuiButtonTypeCenter,
        "OK",
        saflok_scene_mf_classic_mfkey_nonces_info_callback,
        app);

    furi_string_free(temp_str);

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewWidget);
}

bool saflok_scene_mf_classic_mfkey_nonces_info_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    
    FURI_LOG_D(TAG, "on_event");
    
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == GuiButtonTypeCenter) {
            if(mfkey32_logger_save_params(
                   app->mfkey32_logger, SAFLOK_APP_MFKEY32_LOGS_FILE_PATH)) {
                scene_manager_next_scene(app->scene_manager, SaflokSceneMfClassicMfkeyComplete);
            } else {
                scene_manager_search_and_switch_to_previous_scene(
                    app->scene_manager, SaflokSceneStart);
            }
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        const uint32_t prev_scenes[] = {SaflokSceneStart};
        consumed = scene_manager_search_and_switch_to_previous_scene_one_of(
            app->scene_manager, prev_scenes, COUNT_OF(prev_scenes));
    }

    return consumed;
}

void saflok_scene_mf_classic_mfkey_nonces_info_on_exit(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_exit");
    
    mfkey32_logger_free(app->mfkey32_logger);

    widget_reset(app->widget);
}
