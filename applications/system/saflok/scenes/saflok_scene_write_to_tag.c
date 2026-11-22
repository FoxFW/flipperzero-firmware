#include "../saflok_app_i.h"
#include "../helpers/saflok_app_helpers.h"
#include "../helpers/saflok_field_helpers.h"
#include "../helpers/saflok_encryption_helpers.h"
#include "../dialogs/saflok_dialogs.h"
#include <gui/modules/submenu.h>
#include <dialogs/dialogs.h>
#include <core/string.h>
#include <core/log.h>
#include <assets_icons.h>

#undef TAG
#define TAG "SaflokSceneWriteToTag"

void write_to_file(SaflokApp* app);

static void handle_popup_exit(SaflokApp* app) {
    app->popup_visible = false;
    popup_reset(app->popup);

    if(scene_manager_has_previous_scene(app->scene_manager, SaflokSceneExit)) {
        view_dispatcher_stop(app->view_dispatcher);
    } else if(scene_manager_has_previous_scene(app->scene_manager, SaflokSceneTopLevel)) {
        view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventReturnFromWriteToTag);
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, SaflokSceneTopLevel);
    }
}

void saflok_scene_write_to_tag_on_enter(void* context) {
    SaflokApp* app = context;
    if(app->nfc_mode) {
        scene_manager_next_scene(app->scene_manager, SaflokSceneWriteCard);
        return;
    }

    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, "Write to Tag", 63, 3, AlignCenter, AlignTop);
    
    if(app->edit_original) {
        dialog_message_set_text(message, "Are you sure you want to\nmodify the original tag?", 63, 32, AlignCenter, AlignCenter);
    } else {
        dialog_message_set_text(message, "Are you sure you want to\ncreate a modified copy\nof the tag?", 63, 32, AlignCenter, AlignCenter);
    }
    dialog_message_set_buttons(message, "Cancel", NULL, "Write");

    DialogMessageButton choice = dialog_message_show(app->dialogs, message);
    dialog_message_free(message);

    if(choice == DialogMessageButtonRight) {

        write_to_file(app);
    }
    if(scene_manager_has_previous_scene(app->scene_manager, SaflokSceneTopLevel)) {
        view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventReturnFromWriteToTag);
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, SaflokSceneTopLevel);
    }
}

void saflok_scene_write_to_tag_popup_callback(void* context) {
    SaflokApp* app = context;
    handle_popup_exit(app);
}

void show_popup(SaflokApp* app, bool success, const char* message) {
    popup_reset(app->popup);
    app->popup_visible = true;

    if(success) {
        popup_set_header(app->popup, "Saved!", 15, 19, AlignLeft, AlignBottom);
        popup_set_icon(app->popup, 36, 5, &I_DolphinSaved_92x58);
    } else {
        popup_set_header(app->popup, "Error", 64, 16, AlignCenter, AlignCenter);
        popup_set_text(app->popup, message, 64, 32, AlignCenter, AlignCenter);
        popup_set_icon(app->popup, 72, 17, &I_Warning_30x23);
    }

    popup_set_timeout(app->popup, 2000);
    popup_set_context(app->popup, app);
    popup_set_callback(app->popup, saflok_scene_write_to_tag_popup_callback);
    popup_enable_timeout(app->popup);
}

static bool verify_original_file(SaflokApp* app, FuriString* error_msg) {
    if(!storage_common_exists(app->storage, furi_string_get_cstr(app->file_path))) {
        furi_string_set(error_msg, "Original file\nno longer exists!");
        return false;
    }

    NfcDevice* nfc_device = nfc_device_alloc();
    bool valid = true;

    do {
        if(!nfc_device_load(nfc_device, furi_string_get_cstr(app->file_path))) {
            furi_string_set(error_msg, "Cannot read\noriginal file!");
            valid = false;
            break;
        }

        if(nfc_device_get_protocol(nfc_device) != NfcProtocolMfClassic) {
            furi_string_set(error_msg, "Invalid card\nformat!");
            valid = false;
            break;
        }


    } while(false);

    nfc_device_free(nfc_device);
    return valid;
}

void generate_new_filename(SaflokApp* app, FuriString* new_path) {
    FURI_LOG_D(TAG, "Generating new filename from: %s", furi_string_get_cstr(app->file_path));

    FuriString* dirname = furi_string_alloc();
    FuriString* filename = furi_string_alloc();
    path_extract_dirname(furi_string_get_cstr(app->file_path), dirname);
    path_extract_filename_no_ext(furi_string_get_cstr(app->file_path), filename);
    
    furi_string_printf(new_path, "%s/", furi_string_get_cstr(dirname));
    furi_string_cat_str(new_path, furi_string_get_cstr(filename));
    furi_string_cat_printf(new_path, "_mod.nfc");

    int copy_num = 1;
    FuriString* temp_path = furi_string_alloc_set(new_path);
    
    while(storage_common_exists(app->storage, furi_string_get_cstr(new_path))) {
        FURI_LOG_D(TAG, "File exists, trying next number: %d", copy_num);
        
        furi_string_set(new_path, temp_path);
        size_t ext_pos = furi_string_search(new_path, ".nfc");
        if(ext_pos != FURI_STRING_FAILURE) {
            furi_string_left(new_path, ext_pos);
            furi_string_cat_printf(new_path, "_%d.nfc", copy_num++);
        }
    }

    FURI_LOG_D(TAG, "Generated filename: %s", furi_string_get_cstr(new_path));
    furi_string_free(temp_path);
    furi_string_free(dirname);
    furi_string_free(filename);
}

void write_to_file(SaflokApp* app) {
    bool success = false;
    
    popup_reset(app->popup);
    popup_set_header(app->popup, "", 64, 32, AlignCenter, AlignCenter);
    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewPopup);
    FuriString* error_msg = furi_string_alloc();

    do {
        if(app->edit_original) {
            if(!verify_original_file(app, error_msg)) {
                FURI_LOG_E(TAG, "Original file verification failed: %s", furi_string_get_cstr(error_msg));
                break;
            }
        }
        FURI_LOG_D(TAG, "Key type after verify: %d", app->saflok_data->access_control.sub_function.value);
        
        uint8_t encrypted_data[SAFLOK_DATA_LENGTH] = {0};
        
        if (has_unsaved_changes(app)) {
            save_changes(app);
        }

        if(!saflok_encrypt(app->saflok_data->packed_data, encrypted_data)) {
            FURI_LOG_E(TAG, "Failed to encrypt Saflok data");
            furi_string_set(error_msg, "Failed to encrypt data");
            break;
        }

        SaflokMfClassicData* working_data = (SaflokMfClassicData*) nfc_device_get_data(app->saflok_device, NfcProtocolMfClassic);
        working_data->type = MfClassicType1k;
        if(!working_data) {
            FURI_LOG_E(TAG, "Working data is NULL");
            furi_string_set(error_msg, "Invalid device data");
            break;
        }

        memcpy((uint8_t*)working_data->block[1].data, encrypted_data, 16);

        uint8_t temp_data[16];
        memcpy(temp_data, working_data->block[2].data, 16);
        temp_data[0] = encrypted_data[16];
        memcpy((uint8_t*)working_data->block[2].data, temp_data, 16);

        nfc_device_set_data(app->saflok_device, NfcProtocolMfClassic, (MfClassicData*)working_data);
        
        const char* save_path;
        FuriString* new_path = NULL;

        if(app->edit_original) {
            save_path = furi_string_get_cstr(app->file_path);
            FURI_LOG_D(TAG, "Saving to original path: %s", save_path);
        } else {
            new_path = furi_string_alloc();
            
            if(!storage_common_exists(app->storage, SAFLOK_APP_FOLDER)) {
                FURI_LOG_D(TAG, "Creating SAFLOK folder");
                if(!storage_simply_mkdir(app->storage, SAFLOK_APP_FOLDER)) {
                    FURI_LOG_E(TAG, "Failed to create SAFLOK folder");
                    furi_string_set(error_msg, "Failed to create SAFLOK folder");
                    furi_string_free(new_path);
                    break;
                }
            }
            
            generate_new_filename(app, new_path);
            save_path = furi_string_get_cstr(new_path);
            FURI_LOG_D(TAG, "Saving to new path: %s", save_path);
        }

        if(!nfc_device_save(app->saflok_device, save_path)) {
            FURI_LOG_E(TAG, "Failed to save to path: %s", save_path);
            furi_string_set(error_msg, "Failed to save file");
            if(new_path) furi_string_free(new_path);
            break;
        }

        FURI_LOG_I(TAG, "Verifying saved file...");
        NfcDevice* verify_device = nfc_device_alloc();
        if(nfc_device_load(verify_device, save_path)) {
            SaflokMfClassicData* verify_data = (SaflokMfClassicData*)nfc_device_get_data(verify_device, NfcProtocolMfClassic);
            verify_data->type = MfClassicType1k;
            if(verify_data) {
                uint8_t verify_encrypted[SAFLOK_DATA_LENGTH];
                memcpy(verify_encrypted, verify_data->block[1].data, 16);
                verify_encrypted[16] = verify_data->block[2].data[0];
                
                uint8_t* verify_decrypted = malloc(SAFLOK_DATA_LENGTH);
                if(decrypt_data(verify_encrypted, verify_decrypted)) {
                    FURI_LOG_I(TAG, "Verification bytes: byte4=0x%02X, byte5=0x%02X", verify_decrypted[4], verify_decrypted[5]);
                } else {
                    FURI_LOG_E(TAG, "Failed to decrypt verification data");
                }
                free(verify_decrypted);
            }
        } else {
            FURI_LOG_E(TAG, "Failed to load file for verification");
        }
        nfc_device_free(verify_device);

        if(new_path) furi_string_free(new_path);
        success = true;
        
    } while(false);

    if(success) {
        FURI_LOG_I(TAG, "Write operation successful");
        show_popup(app, true, "Data written\nsuccessfully");
    } else {
        FURI_LOG_E(TAG, "Write failed: %s", furi_string_get_cstr(error_msg));
        show_popup(app, false, furi_string_get_cstr(error_msg));
    }

    furi_string_free(error_msg);
    app->is_writing = false;
}

bool saflok_scene_write_to_tag_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;

    switch(event.type) {
        case SceneManagerEventTypeBack: {
            if(scene_manager_has_previous_scene(app->scene_manager, SaflokSceneTopLevel)) {
                view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventReturnFromWriteToTag);
                scene_manager_search_and_switch_to_previous_scene(app->scene_manager, SaflokSceneTopLevel);
            }
            return true;
        }
        default:
            break;
    }

    return false;
}

void saflok_scene_write_to_tag_on_exit(void* context) {
    SaflokApp* app = context;
    popup_reset(app->popup);
    app->popup_visible = false;
    submenu_reset(app->submenu);
}

