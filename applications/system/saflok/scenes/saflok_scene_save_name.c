#include "../saflok_app_i.h"

#include "../helpers/protocol_support/saflok_protocol_support_defs.h"
#include "../helpers/protocol_support/saflok_protocol_support_gui_common.h"

#undef TAG
#define TAG "SaflokSceneSaveName"

void saflok_scene_save_name_text_input_done_callback(void* context) {
    SaflokApp* app = context;

    view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventTextInputDone);
}

void saflok_scene_save_name_on_enter(void* context) { 
    FURI_LOG_D(TAG, "on_enter");
    furi_assert(context);

    SaflokApp* app = context;
    
    FuriString* folder_path = furi_string_alloc();
    TextInput* text_input = app->text_input;

    bool name_is_empty = furi_string_empty(app->file_name);
    if(name_is_empty) {
        furi_string_set(app->file_path, SAFLOK_APP_FOLDER);
        FuriString* prefix = furi_string_alloc();
        furi_string_set(prefix, nfc_device_get_name(app->saflok_device, NfcDeviceNameTypeFull));
        furi_string_replace(prefix, "Mifare", "MF");
        furi_string_replace(prefix, " Classic", "C");
        furi_string_replace(prefix, " Plus", "+");
        furi_string_replace(prefix, " (Unknown)", "");
        furi_string_replace_all(prefix, " ", "_");
        name_generator_make_auto(app->text_store, SAFLOK_TEXT_STORE_SIZE, furi_string_get_cstr(prefix));
        furi_string_free(prefix);
        furi_string_set(folder_path, SAFLOK_APP_FOLDER);
    } else {
        saflok_text_store_set(app, "%s", furi_string_get_cstr(app->file_name));
        path_extract_dirname(furi_string_get_cstr(app->file_path), folder_path);
    }

    text_input_set_header_text(text_input, "Name the card");
    text_input_set_result_callback(text_input, saflok_scene_save_name_text_input_done_callback, app, app->text_store, SAFLOK_NAME_SIZE, name_is_empty);

    ValidatorIsFile* validator_is_file = validator_is_file_alloc_init(furi_string_get_cstr(folder_path), SAFLOK_APP_EXTENSION, furi_string_get_cstr(app->file_name));
    text_input_set_validator(text_input, validator_is_file_callback, validator_is_file);

    furi_string_free(folder_path);

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewTextInput);
}

bool saflok_scene_save_name_on_event(void* context, SceneManagerEvent event) { 
    FURI_LOG_D(TAG, "on_event");
    furi_assert(context);

    SaflokApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SaflokCustomEventTextInputDone) {
            if(!furi_string_empty(app->file_name)) {
                saflok_delete(app);
            }
            furi_string_set(app->file_name, app->text_store);

            if(saflok_save(app)) {
                scene_manager_next_scene(app->scene_manager, SaflokSceneSaveSuccess);
                (void)0; /* dolphin_deed trimmed (Fox has no dolphin service) */

                if(event.type == SceneManagerEventTypeCustom && event.event == SaflokCustomEventTextInputDone) {
                    mf_classic_key_cache_save(app->mfc_key_cache, nfc_device_get_data(app->saflok_device, NfcProtocolMfClassic));
                    consumed = true;
                }
            } else {
                consumed = scene_manager_search_and_switch_to_previous_scene(app->scene_manager, SaflokSceneStart);
            }
        }
    }

    return consumed;
}

void saflok_scene_save_name_on_exit(void* context) {
    FURI_LOG_D(TAG, "on_exit");
    furi_assert(context);

    SaflokApp* app = context;
    void* validator_context = text_input_get_validator_callback_context(app->text_input);
   
    if(validator_context) {
        text_input_set_validator(app->text_input, NULL, NULL);
        validator_is_file_free(validator_context);
    }

    text_input_reset(app->text_input);
}
