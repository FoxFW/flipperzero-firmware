#include "../mfkey_collector_i.h"

void nfc_scene_mf_classic_mfkey_nonces_info_callback(
    GuiButtonType result,
    InputType type,
    void* context) {
    NfcApp* instance = context;

    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(instance->view_dispatcher, result);
    }
}

void nfc_scene_mf_classic_mfkey_nonces_info_on_enter(void* context) {
    NfcApp* instance = context;

    FuriString* temp_str = furi_string_alloc();

    size_t mfkey_params_saved = mfkey32_logger_get_params_num(instance->mfkey32_logger);
    furi_string_printf(temp_str, "Nonce pairs saved: %zu\n", mfkey_params_saved);
    widget_add_string_element(
        instance->widget, 0, 0, AlignLeft, AlignTop, FontPrimary, furi_string_get_cstr(temp_str));
    widget_add_string_element(
        instance->widget, 0, 12, AlignLeft, AlignTop, FontSecondary, "Authenticated sectors:");

    mfkey32_logger_get_params_data(instance->mfkey32_logger, temp_str);
    widget_add_text_scroll_element(
        instance->widget, 0, 22, 128, 25, furi_string_get_cstr(temp_str));
    widget_add_button_element(
        instance->widget,
        GuiButtonTypeCenter,
        "OK",
        nfc_scene_mf_classic_mfkey_nonces_info_callback,
        instance);

    furi_string_free(temp_str);

    view_dispatcher_switch_to_view(instance->view_dispatcher, NfcViewWidget);
}

bool nfc_scene_mf_classic_mfkey_nonces_info_on_event(void* context, SceneManagerEvent event) {
    NfcApp* instance = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == GuiButtonTypeCenter) {
            if(mfkey32_logger_save_params(
                   instance->mfkey32_logger, NFC_APP_MFKEY32_LOGS_FILE_PATH)) {
                scene_manager_next_scene(instance->scene_manager, NfcSceneMfClassicMfkeyComplete);
            } else {
                /* FoxFW: save failed — exit the collector; the loader returns to
                 * the NFC app (equivalent to the original jump back to NfcSceneStart). */
                view_dispatcher_stop(instance->view_dispatcher);
            }
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        /* FoxFW: leaving the collector; go back to the NFC app rather than
         * re-entering the emulation scene beneath this one. */
        view_dispatcher_stop(instance->view_dispatcher);
        consumed = true;
    }

    return consumed;
}

void nfc_scene_mf_classic_mfkey_nonces_info_on_exit(void* context) {
    NfcApp* instance = context;

    mfkey32_logger_free(instance->mfkey32_logger);
    instance->mfkey32_logger = NULL;

    widget_reset(instance->widget);
}
