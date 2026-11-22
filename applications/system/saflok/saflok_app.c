#include "saflok_app_i.h"
#include "helpers/saflok_data.h"
#include "helpers/saflok_field_helpers.h"
#include "helpers/protocol_support/saflok_protocol_support.h"
#include "helpers/saflok_encryption_helpers.h"
#include "helpers/saflok_field_helpers.h"
#include "helpers/saflok_app_helpers.h"

#include "views/aligned_text_box.h"
#include "views/field_list.h"


#undef TAG
#define TAG "SaflokApp"

bool saflok_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    SaflokApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

bool saflok_back_event_callback(void* context) {
    furi_assert(context);
    SaflokApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void saflok_app_rpc_command_callback(const RpcAppSystemEvent* event, void* context) {
    furi_assert(context);
    SaflokApp* app = (SaflokApp*)context;

    furi_assert(app->rpc_ctx);

    if(event->type == RpcAppEventTypeSessionClose) {
        view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventRpcSessionClose);
        rpc_system_app_set_callback(app->rpc_ctx, NULL, NULL);
        app->rpc_ctx = NULL;
    } else if(event->type == RpcAppEventTypeAppExit) {
        view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventRpcExit);
    } else if(event->type == RpcAppEventTypeLoadFile) {
        furi_assert(event->data.type == RpcAppSystemEventDataTypeString);
        furi_string_set(app->file_path, event->data.string);
        view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventRpcLoadFile);
    } else {
        rpc_system_app_confirm(app->rpc_ctx, false);
    }
}

SaflokMfClassicData* saflok_mf_classic_data_alloc(void) {
    SaflokMfClassicData* data = malloc(sizeof(SaflokMfClassicData));
    data->iso14443_3a_data = iso14443_3a_alloc();
    data->type = MfClassicType1k;
    return data;
}

void saflok_mf_classic_data_free(SaflokMfClassicData* data) {
    furi_assert(data);
    iso14443_3a_free(data->iso14443_3a_data);
    free(data);
}

void saflok_changed_alloc(SaflokApp* app) {
    furi_assert(app);
    app->changed = malloc(sizeof(SaflokChanges));
    memset(app->changed, 0, sizeof(SaflokChanges));
}

void saflok_changed_free(SaflokChanges* changed) {
    furi_assert(changed);
    free(changed);
}


SaflokApp* saflok_app_alloc(void) {
    SaflokApp* app = malloc(sizeof(SaflokApp));

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&saflok_scene_handlers, app);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, saflok_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, saflok_back_event_callback);

    app->nfc = nfc_alloc();

    app->detected_protocols = saflok_detected_protocols_alloc();
    app->mfc_key_cache = mf_classic_key_cache_alloc();

    app->saflok_device = nfc_device_alloc();
    nfc_device_set_loading_callback(app->saflok_device, saflok_show_loading_popup, app);

    app->gui = furi_record_open(RECORD_GUI);

    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->storage = furi_record_open(RECORD_STORAGE);

    app->dialogs = furi_record_open(RECORD_DIALOGS);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SaflokViewMenu, submenu_get_view(app->submenu));

    app->start_menu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SaflokViewStartMenu, submenu_get_view(app->start_menu));

    app->topmenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SaflokViewTopMenu, submenu_get_view(app->topmenu));

    app->preset_key_select_menu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SaflokViewPresetKeySelect, submenu_get_view(app->preset_key_select_menu));

    app->dialog_ex = dialog_ex_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SaflokViewDialogEx, dialog_ex_get_view(app->dialog_ex));

    app->popup = popup_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SaflokViewPopup, popup_get_view(app->popup));

    app->loading = loading_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SaflokViewLoading, loading_get_view(app->loading));

    app->text_input = text_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SaflokViewTextInput, text_input_get_view(app->text_input));

    app->byte_input = byte_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SaflokViewByteInput, byte_input_get_view(app->byte_input));

    app->text_box = text_box_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SaflokViewTextBox, text_box_get_view(app->text_box));
    app->text_box_store = furi_string_alloc();

    app->widget = widget_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SaflokViewWidget, widget_get_view(app->widget));

    app->dict_attack = dict_attack_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SaflokViewDictAttack, dict_attack_get_view(app->dict_attack));

    app->detect_reader = detect_reader_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        SaflokViewDetectReader,
        detect_reader_get_view(app->detect_reader));

    app->hex_input = hex_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        SaflokViewHexInput,
        hex_input_get_view(app->hex_input));
    
    app->number_input = number_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        SaflokViewNumberInput,
        number_input_get_view(app->number_input));
    
    app->date_input = saflok_date_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        SaflokViewDateInput,
        saflok_date_input_get_view(app->date_input));
    
    app->display_settings_list = option_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        SaflokViewDisplaySettingsList,
        option_list_get_view(app->display_settings_list));
    
    app->pass_levels_list = option_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        SaflokViewPassLevelsList,
        option_list_get_view(app->pass_levels_list));
    
    app->restrictions_list = option_list_alloc();
    app->restrictions_context = NULL;
    view_dispatcher_add_view(
        app->view_dispatcher,
        SaflokViewRestrictionsList,
        option_list_get_view(app->restrictions_list));
    
    app->field_list = field_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        SaflokViewFieldList,
        field_list_get_view(app->field_list));
    
    app->key_level_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        SaflokViewKeyLevel,
        variable_item_list_get_view(app->key_level_list));
    
    app->sub_function_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        SaflokViewSubFunction,
        variable_item_list_get_view(app->sub_function_list));
    
    app->aligned_text_box = aligned_text_box_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        SaflokViewAlignedTextBox,
        aligned_text_box_get_view(app->aligned_text_box));
    
    app->mf_classic_data = saflok_mf_classic_data_alloc();
    app->saflok_data = saflok_data_alloc(app);
    app->file_path = furi_string_alloc_set(NFC_APP_FOLDER);
    app->file_name = furi_string_alloc();
    
    app->popup_visible = false;
    app->is_writing = false;
    saflok_changed_alloc(app);
    app->previous_field_index = 0;
    app->text_input_buffer = NULL;
    app->edit_buffer = NULL;
    
    app->display_settings = malloc(sizeof(DisplaySettings));
    memset(app->display_settings, 0, sizeof(DisplaySettings));
    app->display_settings->key_record_hex_format = true;
    app->display_settings->sequence_combo_hex_format = true;

    return app;
}

void saflok_app_free(SaflokApp* app) {
    furi_assert(app);

    if(app->rpc_ctx) {
        rpc_system_app_send_exited(app->rpc_ctx);
        rpc_system_app_set_callback(app->rpc_ctx, NULL, NULL);
    }

    if(app->nfc) {
        nfc_free(app->nfc);
        app->nfc = NULL;
    }

    if(app->detected_protocols) {
        saflok_detected_protocols_free(app->detected_protocols);
        app->detected_protocols = NULL;
    }

    if(app->mfc_key_cache) {
        mf_classic_key_cache_free(app->mfc_key_cache);
        app->mfc_key_cache = NULL;
    }

    if(app->saflok_device) {
        nfc_device_free(app->saflok_device);
        app->saflok_device = NULL;
    }

    if(app->submenu) {
        view_dispatcher_remove_view(app->view_dispatcher, SaflokViewMenu);
        submenu_free(app->submenu);
        app->submenu = NULL;
    }

    if(app->start_menu) {
        view_dispatcher_remove_view(app->view_dispatcher, SaflokViewStartMenu);
        submenu_free(app->start_menu);
        app->start_menu = NULL;
    }

    if(app->topmenu) {
        view_dispatcher_remove_view(app->view_dispatcher, SaflokViewTopMenu);
        submenu_free(app->topmenu);
        app->topmenu = NULL;
    }

    if(app->preset_key_select_menu) {
        view_dispatcher_remove_view(app->view_dispatcher, SaflokViewPresetKeySelect);
        submenu_free(app->preset_key_select_menu);
        app->preset_key_select_menu = NULL;
    }

    if(app->dialog_ex) {
        view_dispatcher_remove_view(app->view_dispatcher, SaflokViewDialogEx);
        dialog_ex_free(app->dialog_ex);
        app->dialog_ex = NULL;
    }

    if(app->popup) {
        view_dispatcher_remove_view(app->view_dispatcher, SaflokViewPopup);
        popup_free(app->popup);
        app->popup = NULL;
    }

    if(app->loading) {
        view_dispatcher_remove_view(app->view_dispatcher, SaflokViewLoading);
        loading_free(app->loading);
        app->loading = NULL;
    }

    if(app->text_input) {
        view_dispatcher_remove_view(app->view_dispatcher, SaflokViewTextInput);
        text_input_free(app->text_input);
        app->text_input = NULL;
    }

    if(app->byte_input) {
        view_dispatcher_remove_view(app->view_dispatcher, SaflokViewByteInput);
        byte_input_free(app->byte_input);
        app->byte_input = NULL;
    }

    if(app->text_box) {
        view_dispatcher_remove_view(app->view_dispatcher, SaflokViewTextBox);
        text_box_free(app->text_box);
        app->text_box = NULL;
    }

    if(app->text_box_store) {
        furi_string_free(app->text_box_store);
        app->text_box_store = NULL;
    }

    if(app->widget) {
        view_dispatcher_remove_view(app->view_dispatcher, SaflokViewWidget);
        widget_free(app->widget);
        app->widget = NULL;
    }

    if(app->dict_attack) {
        view_dispatcher_remove_view(app->view_dispatcher, SaflokViewDictAttack);
        dict_attack_free(app->dict_attack);
        app->dict_attack = NULL;
    }

    if(app->detect_reader) {
        view_dispatcher_remove_view(app->view_dispatcher, SaflokViewDetectReader);
        detect_reader_free(app->detect_reader);
        app->detect_reader = NULL;
    }

    if(app->display_settings) {
        free(app->display_settings);
        app->display_settings = NULL;
    }

    if(app->changed) {
        saflok_changed_free(app->changed);
        app->changed = NULL;
    }

    if(app->mf_classic_data) {
        saflok_mf_classic_data_free(app->mf_classic_data);
        app->mf_classic_data = NULL;
    }

    if(app->saflok_data) {
        saflok_data_free(app->saflok_data);
        app->saflok_data = NULL;
    }

    if(app->hex_input) {
        view_dispatcher_remove_view(app->view_dispatcher, SaflokViewHexInput);
        hex_input_free(app->hex_input);
        app->hex_input = NULL;
    }

    if(app->number_input) {
        view_dispatcher_remove_view(app->view_dispatcher, SaflokViewNumberInput);
        number_input_free(app->number_input);
        app->number_input = NULL;
    }

    if(app->date_input) {
        view_dispatcher_remove_view(app->view_dispatcher, SaflokViewDateInput);
        saflok_date_input_free(app->date_input);
        app->date_input = NULL;
    }

    if(app->display_settings_list) {
        view_dispatcher_remove_view(app->view_dispatcher, SaflokViewDisplaySettingsList);
        option_list_free(app->display_settings_list);
        app->display_settings_list = NULL;
    }

    if(app->pass_levels_list) {
        view_dispatcher_remove_view(app->view_dispatcher, SaflokViewPassLevelsList);
        option_list_free(app->pass_levels_list);
        app->pass_levels_list = NULL;
    }

    if(app->restrictions_list) {
        view_dispatcher_remove_view(app->view_dispatcher, SaflokViewRestrictionsList);
        option_list_free(app->restrictions_list);
        app->restrictions_list = NULL;
    }

    if(app->restrictions_context) {
        free(app->restrictions_context);
        app->restrictions_context = NULL;
    }
    
    if(app->field_list) {
        view_dispatcher_remove_view(app->view_dispatcher, SaflokViewFieldList);
        field_list_free(app->field_list);
        app->field_list = NULL;
    }

    if(app->key_level_list) {
        view_dispatcher_remove_view(app->view_dispatcher, SaflokViewKeyLevel);
        variable_item_list_free(app->key_level_list);
        app->key_level_list = NULL;
    }

    if(app->sub_function_list) {
        view_dispatcher_remove_view(app->view_dispatcher, SaflokViewSubFunction);
        variable_item_list_free(app->sub_function_list);
        app->sub_function_list = NULL;
    }

    if(app->aligned_text_box) {
        view_dispatcher_remove_view(app->view_dispatcher, SaflokViewAlignedTextBox);
        aligned_text_box_free(app->aligned_text_box);
        app->aligned_text_box = NULL;
    }

    FURI_LOG_D(TAG, "calling view_dispatcher_free");
    view_dispatcher_free(app->view_dispatcher);
    FURI_LOG_D(TAG, "finished view_dispatcher_free");

    FURI_LOG_D(TAG, "calling scene_manager_free");
    scene_manager_free(app->scene_manager);
    FURI_LOG_D(TAG, "finished scene_manager_free");

    FURI_LOG_D(TAG, "calling furi_record_close");
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_NOTIFICATION);

    furi_record_close(RECORD_GUI);
    FURI_LOG_D(TAG, "finished furi_record_close");

    app->gui = NULL;

    app->notifications = NULL;

    FURI_LOG_D(TAG, "calling furi_string_free");
    furi_string_free(app->file_path);
    furi_string_free(app->file_name);
    app->file_path = NULL;
    app->file_name = NULL;
    FURI_LOG_D(TAG, "finished furi_string_free");

    FURI_LOG_D(TAG, "calling free(app)");
    free(app);
}

void saflok_text_store_set(SaflokApp* app, const char* text, ...) {
    va_list args;
    va_start(args, text);

    vsnprintf(app->text_store, sizeof(app->text_store), text, args);

    va_end(args);
}

void saflok_text_store_clear(SaflokApp* app) {
    memset(app->text_store, 0, sizeof(app->text_store));
}

void saflok_blink_read_start(SaflokApp* app) {
    notification_message(app->notifications, &sequence_blink_start_yellow);
}

void saflok_blink_emulate_start(SaflokApp* app) {
    notification_message(app->notifications, &sequence_blink_start_magenta);
}

void saflok_blink_detect_start(SaflokApp* app) {
    notification_message(app->notifications, &sequence_blink_start_cyan);
}

void saflok_blink_stop(SaflokApp* app) {
    notification_message(app->notifications, &sequence_blink_stop);
}

void saflok_make_app_folders(SaflokApp* app) {
    furi_assert(app);

    if(!storage_simply_mkdir(app->storage, SAFLOK_APP_FOLDER)) {
        dialog_message_show_storage_error(app->dialogs, "Cannot create\napp folder");
    }
}

bool has_unsaved_changes(SaflokApp* app) {
    furi_assert(app);

    if (app->changed->key_level         ||
        app->changed->sub_function      ||
        app->changed->key_id            ||
        app->changed->led_warning       ||
        app->changed->key_record        ||
        app->changed->pass_levels       ||
        app->changed->opening_key       ||
        app->changed->sequence_combo    ||
        app->changed->override_deadbolt ||
        app->changed->restrictions      ||
        app->changed->creation_date     ||
        app->changed->expiration_date   ||
        app->changed->property_number   ||
        app->changed->checksum) {

        return true;
    }
    return false;
}

void save_changes(SaflokApp* app) {
    furi_assert(app);

    if (app->changed->key_level) {
        saflok_pack_current_key_level(app);
    }
    if (app->changed->sub_function) {
        saflok_pack_current_sub_function(app);
    }
    if (app->changed->led_warning) {
        saflok_pack_current_led_warning(app);
    }
    if (app->changed->key_id) {
        saflok_pack_current_key_id(app);
    }
    if (app->changed->key_record) {
        saflok_pack_current_key_record(app);
    }
    if (app->changed->opening_key) {
        saflok_pack_current_opening_key(app);
    }
    if (app->changed->pass_levels) {
        saflok_pack_current_pass_levels(app);
    }
    if (app->changed->sequence_combo) {
        saflok_pack_current_sequence_combo(app);
    }
    if (app->changed->override_deadbolt) {
        saflok_pack_current_override_deadbolt(app);
    }
    if (app->changed->restrictions) {
        saflok_pack_current_restrictions(app);
    }
    if (app->changed->creation_date) {
        saflok_pack_current_creation_date(app);
    }
    if (app->changed->expiration_date) {
        saflok_pack_current_expiration_date(app);
    }
    if (app->changed->property_number) {
        saflok_pack_current_property_number(app);
    }
    if (app->changed->checksum) {
        saflok_pack_current_checksum(app);
    }
}

bool saflok_save_file(SaflokApp* app, FuriString* path) {
    furi_assert(app);
    furi_assert(path);

    bool result = nfc_device_save(app->saflok_device, furi_string_get_cstr(app->file_path));

    if(!result) {
        dialog_message_show_storage_error(app->dialogs, "Cannot save\nkey file");
    }

    return result;
}

static bool saflok_set_shadow_file_path(FuriString* file_path, FuriString* shadow_file_path) {
    furi_assert(file_path);
    furi_assert(shadow_file_path);

    bool shadow_file_path_set = false;
    if(furi_string_end_with(file_path, SAFLOK_APP_SHADOW_EXTENSION)) {
        furi_string_set(shadow_file_path, file_path);
        shadow_file_path_set = true;
    } else if(furi_string_end_with(file_path, SAFLOK_APP_EXTENSION)) {
        size_t path_len = furi_string_size(file_path);
        furi_string_set_n(shadow_file_path, file_path, 0, path_len - 4);
        furi_string_cat_printf(shadow_file_path, "%s", SAFLOK_APP_SHADOW_EXTENSION);
        shadow_file_path_set = true;
    }

    return shadow_file_path_set;
}

static bool saflok_has_shadow_file_internal(SaflokApp* app, FuriString* path) {
    furi_assert(path);

    bool has_shadow_file = false;
    FuriString* shadow_file_path = furi_string_alloc();
    do {
        if(furi_string_empty(path)) break;
        if(!saflok_set_shadow_file_path(path, shadow_file_path)) break;
        has_shadow_file =
            storage_common_exists(app->storage, furi_string_get_cstr(shadow_file_path));
    } while(false);

    furi_string_free(shadow_file_path);

    return has_shadow_file;
}

bool saflok_has_shadow_file(SaflokApp* app) {
    furi_assert(app);

    return saflok_has_shadow_file_internal(app, app->file_path);
}

static bool saflok_save_internal(SaflokApp* app, const char* extension) {
    furi_assert(app);
    furi_assert(extension);

    bool result = false;

    saflok_make_app_folders(app);

    if(furi_string_end_with(app->file_path, SAFLOK_APP_EXTENSION) ||
       (furi_string_end_with(app->file_path, SAFLOK_APP_SHADOW_EXTENSION))) {
        size_t filename_start = furi_string_search_rchar(app->file_path, '/');
        furi_string_left(app->file_path, filename_start);
    }

    furi_string_cat_printf(
        app->file_path, "/%s%s", furi_string_get_cstr(app->file_name), extension);

    result = saflok_save_file(app, app->file_path);

    return result;
}

bool saflok_save_shadow_file(SaflokApp* app) {
    furi_assert(app);

    return saflok_save_internal(app, SAFLOK_APP_SHADOW_EXTENSION);
}

bool saflok_save(SaflokApp* app) {
    furi_assert(app);

    return saflok_save_internal(app, SAFLOK_APP_EXTENSION);
}

bool saflok_load_file(SaflokApp* app, FuriString* path, bool show_dialog) {
    furi_assert(app);
    furi_assert(path);

    FuriString* load_path = furi_string_alloc();
    if(saflok_has_shadow_file_internal(app, path)) {
        saflok_set_shadow_file_path(path, load_path);
    } else if(furi_string_end_with(path, SAFLOK_APP_SHADOW_EXTENSION)) {
        size_t path_len = furi_string_size(path);
        furi_string_set_n(load_path, path, 0, path_len - 4);
        furi_string_cat_printf(load_path, "%s", SAFLOK_APP_EXTENSION);
    } else {
        furi_string_set(load_path, path);
    }

    if (!nfc_device_load(app->saflok_device, furi_string_get_cstr(load_path))) {
        if (show_dialog) {
            dialog_message_show_storage_error(app->dialogs, "Cannot load\nkey file");
        } else {
            FURI_LOG_E(TAG, "Cannot load key file");
        }
        return false;
    }
    
    uint8_t encrypted_data[SAFLOK_DATA_LENGTH] = {0};
    
    app->mf_classic_data = (SaflokMfClassicData*)nfc_device_get_data(app->saflok_device, NfcProtocolMfClassic);
    app->mf_classic_data->type = MfClassicType1k;
    
    memcpy(encrypted_data, app->mf_classic_data->block[1].data, 16);
    encrypted_data[16] = app->mf_classic_data->block[2].data[0];
    saflok_decrypt(encrypted_data, app->saflok_data->packed_data, sizeof(encrypted_data));
    
    if (!saflok_unpack_data(app->saflok_data->packed_data, app)) {
        if (show_dialog) {
            dialog_message_show_storage_error(app->dialogs, "Failed to\nunpack data");
        } else {
            FURI_LOG_E(TAG, "Failed to unpack data");
        }
        return false;
    }

    path_extract_filename(load_path, app->file_name, true);

    furi_string_free(load_path);

    return true;
}

bool saflok_delete(SaflokApp* app) {
    furi_assert(app);

    if(saflok_has_shadow_file(app)) {
        saflok_delete_shadow_file(app);
    }

    if(furi_string_end_with_str(app->file_path, SAFLOK_APP_SHADOW_EXTENSION)) {
        size_t path_len = furi_string_size(app->file_path);
        furi_string_replace_at(app->file_path, path_len - 4, 4, SAFLOK_APP_EXTENSION);
    }

    return storage_simply_remove(app->storage, furi_string_get_cstr(app->file_path));
}

bool saflok_delete_shadow_file(SaflokApp* app) {
    furi_assert(app);

    FuriString* shadow_file_path = furi_string_alloc();

    bool result = saflok_set_shadow_file_path(app->file_path, shadow_file_path) &&
                  storage_simply_remove(app->storage, furi_string_get_cstr(shadow_file_path));

    furi_string_free(shadow_file_path);
    return result;
}

bool saflok_load_from_file_select(SaflokApp* app) {
    furi_assert(app);

    DialogsFileBrowserOptions browser_options;
    dialog_file_browser_set_basic_options(&browser_options, SAFLOK_APP_EXTENSION, &I_Nfc_10px);
    browser_options.base_path = NFC_APP_FOLDER;
    browser_options.hide_dot_files = true;

    bool success = false;
    do {
        if(!dialog_file_browser_show(
               app->dialogs, app->file_path, app->file_path, &browser_options))
            break;
        success = saflok_load_file(app, app->file_path, true);
    } while(!success);

    return success;
}

void saflok_show_loading_popup(void* context, bool show) {
    SaflokApp* app = context;

    if(show) {
        furi_timer_set_thread_priority(FuriTimerThreadPriorityElevated);
        view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewLoading);
    } else {
        furi_timer_set_thread_priority(FuriTimerThreadPriorityNormal);
    }
}

void saflok_append_filename_string_when_present(SaflokApp* app, FuriString* string) {
    furi_assert(app);
    furi_assert(string);

    if(!furi_string_empty(app->file_name)) {
        furi_string_cat_printf(string, "Name: %s\n", furi_string_get_cstr(app->file_name));
    }
}

static bool saflok_is_hal_ready(void) {
    if(furi_hal_nfc_is_hal_ready() != FuriHalNfcErrorNone) {
        DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
        DialogMessage* message = dialog_message_alloc();
        dialog_message_set_header(message, "Error: NFC Chip Failed", 64, 0, AlignCenter, AlignTop);
        dialog_message_set_text(
            message, "Send error photo via\nsupport.flipper.net", 0, 63, AlignLeft, AlignBottom);
        dialog_message_set_icon(message, &I_err_09, 128 - 25, 64 - 25);
        dialog_message_show(dialogs, message);
        dialog_message_free(message);
        furi_record_close(RECORD_DIALOGS);
        return false;
    } else {
        return true;
    }
}

static void saflok_show_initial_scene_for_device(SaflokApp* app) {
    uint32_t scene = SaflokSceneEmulate;
    scene_manager_next_scene(app->scene_manager, scene);
}

int32_t saflok_app(void* p) {
    if(!saflok_is_hal_ready()) return 0;

    const char* args = p;

    SaflokApp* app = saflok_app_alloc();

    bool is_favorite = false; /* Fox has no Momentum favorites */
    if(args && strlen(args)) {
        if(sscanf(args, "RPC %p", &app->rpc_ctx) == 1) {
            rpc_system_app_set_callback(app->rpc_ctx, saflok_app_rpc_command_callback, app);
            rpc_system_app_send_started(app->rpc_ctx);
            view_dispatcher_attach_to_gui(
                app->view_dispatcher, app->gui, ViewDispatcherTypeDesktop);
            scene_manager_next_scene(app->scene_manager, SaflokSceneRpc);
        } else {
            view_dispatcher_attach_to_gui(
                app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

            furi_string_set(app->file_path, args);
            if(saflok_load_file(app, app->file_path, true)) {
                app->fav_timeout = is_favorite;
                saflok_show_initial_scene_for_device(app);
            } else {
                view_dispatcher_stop(app->view_dispatcher);
            }
        }
    } else {
        view_dispatcher_attach_to_gui(
            app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
        scene_manager_next_scene(app->scene_manager, SaflokSceneStart);
    }

    view_dispatcher_run(app->view_dispatcher);

    saflok_app_free(app);

    return 0;
}
