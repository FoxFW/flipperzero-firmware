#include "../saflok_app_i.h"

#undef TAG
#define TAG "SaflokSceneMfClassicKeysAdd"

void saflok_scene_mf_classic_keys_add_byte_input_callback(void* context) {
    SaflokApp* app = context;

    view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventByteInputDone);
}

void saflok_scene_mf_classic_keys_add_on_enter(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_enter");

    ByteInput* byte_input = app->byte_input;
    byte_input_set_header_text(byte_input, "Enter the key in hex");
    byte_input_set_result_callback(
        byte_input,
        saflok_scene_mf_classic_keys_add_byte_input_callback,
        NULL,
        app,
        app->byte_input_store,
        sizeof(MfClassicKey));
    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewByteInput);
}

bool saflok_scene_mf_classic_keys_add_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    
    FURI_LOG_D(TAG, "on_event");
    
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SaflokCustomEventByteInputDone) {
            KeysDict* dict = keys_dict_alloc(
                SAFLOK_APP_MF_CLASSIC_DICT_USER_PATH, KeysDictModeOpenAlways, sizeof(MfClassicKey));

            MfClassicKey key = {};
            memcpy(key.data, app->byte_input_store, sizeof(MfClassicKey));
            if(keys_dict_is_key_present(dict, key.data, sizeof(MfClassicKey))) {
                scene_manager_next_scene(
                    app->scene_manager, SaflokSceneMfClassicKeysWarnDuplicate);
            } else if(keys_dict_add_key(dict, key.data, sizeof(MfClassicKey))) {
                scene_manager_next_scene(app->scene_manager, SaflokSceneSaveSuccess);
                (void)0; /* dolphin_deed trimmed (Fox has no dolphin service) */
            } else {
                scene_manager_previous_scene(app->scene_manager);
            }

            keys_dict_free(dict);
            consumed = true;
        }
    }

    return consumed;
}

void saflok_scene_mf_classic_keys_add_on_exit(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_exit");
    
    byte_input_set_result_callback(app->byte_input, NULL, NULL, NULL, NULL, 0);
    byte_input_set_header_text(app->byte_input, "");
}
