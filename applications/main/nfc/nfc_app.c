#include "nfc_app_i.h"
#include "nfc_icons.h"
#include "api/nfc_app_api_interface.h"
#include "helpers/protocol_support/nfc_protocol_support.h"


#include <loader/firmware_api/firmware_api.h>
#include <cli/cli_vcp.h>
#include <furi_hal_usb.h>
#include <rpc/rpc_gui_screen_suppress.h>

bool nfc_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    NfcApp* nfc = context;
    return scene_manager_handle_custom_event(nfc->scene_manager, event);
}

bool nfc_back_event_callback(void* context) {
    furi_assert(context);
    NfcApp* nfc = context;
    return scene_manager_handle_back_event(nfc->scene_manager);
}

static void nfc_app_rpc_command_callback(const RpcAppSystemEvent* event, void* context) {
    furi_assert(context);
    NfcApp* nfc = (NfcApp*)context;

    furi_assert(nfc->rpc_ctx);

    if(event->type == RpcAppEventTypeSessionClose) {
        view_dispatcher_send_custom_event(nfc->view_dispatcher, NfcCustomEventRpcSessionClose);
        rpc_system_app_set_callback(nfc->rpc_ctx, NULL, NULL);
        nfc->rpc_ctx = NULL;
    } else if(event->type == RpcAppEventTypeAppExit) {
        view_dispatcher_send_custom_event(nfc->view_dispatcher, NfcCustomEventRpcExit);
    } else if(event->type == RpcAppEventTypeLoadFile) {
        furi_assert(event->data.type == RpcAppSystemEventDataTypeString);
        furi_string_set(nfc->file_path, event->data.string);
        view_dispatcher_send_custom_event(nfc->view_dispatcher, NfcCustomEventRpcLoadFile);
    } else {
        rpc_system_app_confirm(nfc->rpc_ctx, false);
    }
}

NfcApp* nfc_app_alloc(void) {
    NfcApp* instance = malloc(sizeof(NfcApp));

    instance->gui = furi_record_open(RECORD_GUI);

    instance->startup_loading = loading_alloc();
    instance->startup_holder = view_holder_alloc();
    view_holder_attach_to_gui(instance->startup_holder, instance->gui);
    view_holder_set_view(
        instance->startup_holder, loading_get_view(instance->startup_loading));

    instance->view_dispatcher = view_dispatcher_alloc();
    instance->scene_manager = scene_manager_alloc(&nfc_scene_handlers, instance);
    view_dispatcher_set_event_callback_context(instance->view_dispatcher, instance);
    view_dispatcher_set_custom_event_callback(
        instance->view_dispatcher, nfc_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        instance->view_dispatcher, nfc_back_event_callback);

    instance->nfc = nfc_alloc();

    instance->api_resolver = composite_api_resolver_alloc();
    composite_api_resolver_add(instance->api_resolver, firmware_api_interface);
    composite_api_resolver_add(instance->api_resolver, nfc_application_api_interface);

    instance->detected_protocols = nfc_detected_protocols_alloc();
    instance->felica_auth = felica_auth_alloc();
    instance->mf_ul_auth = mf_ultralight_auth_alloc();
    instance->slix_unlock = slix_unlock_alloc();
    instance->mfc_key_cache = mf_classic_key_cache_alloc();
    instance->nfc_supported_cards = nfc_supported_cards_alloc(instance->api_resolver);

    instance->nfc_device = nfc_device_alloc();
    nfc_device_set_loading_callback(instance->nfc_device, nfc_show_loading_popup, instance);

    instance->notifications = furi_record_open(RECORD_NOTIFICATION);

    instance->storage = furi_record_open(RECORD_STORAGE);

    instance->dialogs = furi_record_open(RECORD_DIALOGS);

    instance->submenu = submenu_alloc();
    view_dispatcher_add_view(
        instance->view_dispatcher, NfcViewMenu, submenu_get_view(instance->submenu));

    instance->dialog_ex = dialog_ex_alloc();
    view_dispatcher_add_view(
        instance->view_dispatcher, NfcViewDialogEx, dialog_ex_get_view(instance->dialog_ex));

    instance->popup = popup_alloc();
    view_dispatcher_add_view(
        instance->view_dispatcher, NfcViewPopup, popup_get_view(instance->popup));

    instance->loading = loading_alloc();
    view_dispatcher_add_view(
        instance->view_dispatcher, NfcViewLoading, loading_get_view(instance->loading));

    instance->text_input = text_input_alloc();
    view_dispatcher_add_view(
        instance->view_dispatcher, NfcViewTextInput, text_input_get_view(instance->text_input));

    instance->byte_input = byte_input_alloc();
    view_dispatcher_add_view(
        instance->view_dispatcher, NfcViewByteInput, byte_input_get_view(instance->byte_input));

    instance->text_box = text_box_alloc();
    view_dispatcher_add_view(
        instance->view_dispatcher, NfcViewTextBox, text_box_get_view(instance->text_box));
    instance->text_box_store = furi_string_alloc();

    instance->widget = widget_alloc();
    view_dispatcher_add_view(
        instance->view_dispatcher, NfcViewWidget, widget_get_view(instance->widget));

    instance->dict_attack = dict_attack_alloc();
    view_dispatcher_add_view(
        instance->view_dispatcher, NfcViewDictAttack, dict_attack_get_view(instance->dict_attack));


    instance->iso14443_3a_edit_data = iso14443_3a_alloc();
    instance->file_path = furi_string_alloc_set(NFC_APP_FOLDER);
    instance->file_name = furi_string_alloc();

    return instance;
}

void nfc_cli_soft_lock(NfcApp* instance) {
    furi_assert(instance);
    if(instance->cli_sessions_soft_locked) return;

    FURI_LOG_I("Nfc", "Read entry: detaching USB/CLI + suppressing screen stream to free RAM");
    rpc_gui_screen_stream_set_placeholder_label("NFC");
    rpc_gui_screen_stream_set_suppressed(true);

    CliVcp* cli_vcp = furi_record_open(RECORD_CLI_VCP);
    cli_vcp_session_lock(cli_vcp);
    furi_record_close(RECORD_CLI_VCP);

    instance->cli_hard_disconnect_usb_config = furi_hal_usb_get_config();
    furi_hal_usb_unlock();
    furi_hal_usb_set_config(NULL, NULL);
    instance->cli_sessions_soft_locked = true;
}

void nfc_cli_soft_unlock(NfcApp* instance) {
    furi_assert(instance);
    if(!instance->cli_sessions_soft_locked) return;

    furi_hal_usb_set_config(
        (FuriHalUsbInterface*)instance->cli_hard_disconnect_usb_config, NULL);
    instance->cli_hard_disconnect_usb_config = NULL;

    CliVcp* cli_vcp = furi_record_open(RECORD_CLI_VCP);
    cli_vcp_session_unlock(cli_vcp);
    furi_record_close(RECORD_CLI_VCP);

    rpc_gui_screen_stream_set_suppressed(false);
    rpc_gui_screen_stream_set_placeholder_label(NULL);
    instance->cli_sessions_soft_locked = false;
    FURI_LOG_I("Nfc", "Read exit: restored USB/CLI + screen stream");
}

void nfc_app_idle_views_unload(NfcApp* instance) {
    furi_assert(instance);
    if(instance->submenu == NULL) return;
    view_dispatcher_remove_view(instance->view_dispatcher, NfcViewMenu);
    submenu_free(instance->submenu);
    instance->submenu = NULL;
    view_dispatcher_remove_view(instance->view_dispatcher, NfcViewDialogEx);
    dialog_ex_free(instance->dialog_ex);
    instance->dialog_ex = NULL;
    view_dispatcher_remove_view(instance->view_dispatcher, NfcViewTextInput);
    text_input_free(instance->text_input);
    instance->text_input = NULL;
    view_dispatcher_remove_view(instance->view_dispatcher, NfcViewByteInput);
    byte_input_free(instance->byte_input);
    instance->byte_input = NULL;
    view_dispatcher_remove_view(instance->view_dispatcher, NfcViewTextBox);
    text_box_free(instance->text_box);
    instance->text_box = NULL;
    view_dispatcher_remove_view(instance->view_dispatcher, NfcViewWidget);
    widget_free(instance->widget);
    instance->widget = NULL;
    view_dispatcher_remove_view(instance->view_dispatcher, NfcViewDictAttack);
    dict_attack_free(instance->dict_attack);
    instance->dict_attack = NULL;
}

void nfc_app_idle_views_reload(NfcApp* instance) {
    furi_assert(instance);
    if(instance->submenu != NULL) return;
    instance->submenu = submenu_alloc();
    view_dispatcher_add_view(
        instance->view_dispatcher, NfcViewMenu, submenu_get_view(instance->submenu));
    instance->dialog_ex = dialog_ex_alloc();
    view_dispatcher_add_view(
        instance->view_dispatcher, NfcViewDialogEx, dialog_ex_get_view(instance->dialog_ex));
    instance->text_input = text_input_alloc();
    view_dispatcher_add_view(
        instance->view_dispatcher, NfcViewTextInput, text_input_get_view(instance->text_input));
    instance->byte_input = byte_input_alloc();
    view_dispatcher_add_view(
        instance->view_dispatcher, NfcViewByteInput, byte_input_get_view(instance->byte_input));
    instance->text_box = text_box_alloc();
    view_dispatcher_add_view(
        instance->view_dispatcher, NfcViewTextBox, text_box_get_view(instance->text_box));
    instance->widget = widget_alloc();
    view_dispatcher_add_view(
        instance->view_dispatcher, NfcViewWidget, widget_get_view(instance->widget));
    instance->dict_attack = dict_attack_alloc();
    view_dispatcher_add_view(
        instance->view_dispatcher, NfcViewDictAttack, dict_attack_get_view(instance->dict_attack));
}

void nfc_app_free(NfcApp* instance) {
    furi_assert(instance);

    nfc_cli_soft_unlock(instance);

    nfc_app_idle_views_reload(instance);

    if(instance->rpc_ctx) {
        rpc_system_app_send_exited(instance->rpc_ctx);
        rpc_system_app_set_callback(instance->rpc_ctx, NULL, NULL);
    }

    nfc_free(instance->nfc);

    nfc_detected_protocols_free(instance->detected_protocols);
    felica_auth_free(instance->felica_auth);
    mf_ultralight_auth_free(instance->mf_ul_auth);
    slix_unlock_free(instance->slix_unlock);
    mf_classic_key_cache_free(instance->mfc_key_cache);
    nfc_supported_cards_free(instance->nfc_supported_cards);
    if(instance->protocol_support) {
        nfc_protocol_support_free(instance);
    }

    composite_api_resolver_free(instance->api_resolver);
    instance->api_resolver = NULL;

    nfc_device_free(instance->nfc_device);

    view_dispatcher_remove_view(instance->view_dispatcher, NfcViewMenu);
    submenu_free(instance->submenu);

    view_dispatcher_remove_view(instance->view_dispatcher, NfcViewDialogEx);
    dialog_ex_free(instance->dialog_ex);

    view_dispatcher_remove_view(instance->view_dispatcher, NfcViewPopup);
    popup_free(instance->popup);

    view_dispatcher_remove_view(instance->view_dispatcher, NfcViewLoading);
    loading_free(instance->loading);

    view_dispatcher_remove_view(instance->view_dispatcher, NfcViewTextInput);
    text_input_free(instance->text_input);

    view_dispatcher_remove_view(instance->view_dispatcher, NfcViewByteInput);
    byte_input_free(instance->byte_input);

    view_dispatcher_remove_view(instance->view_dispatcher, NfcViewTextBox);
    text_box_free(instance->text_box);
    furi_string_free(instance->text_box_store);

    view_dispatcher_remove_view(instance->view_dispatcher, NfcViewWidget);
    widget_free(instance->widget);

    view_dispatcher_remove_view(instance->view_dispatcher, NfcViewDictAttack);
    dict_attack_free(instance->dict_attack);


    view_dispatcher_free(instance->view_dispatcher);

    scene_manager_free(instance->scene_manager);

    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_NOTIFICATION);

    if(instance->startup_holder) {
        view_holder_set_view(instance->startup_holder, NULL);
        view_holder_free(instance->startup_holder);
        instance->startup_holder = NULL;
    }
    if(instance->startup_loading) {
        loading_free(instance->startup_loading);
        instance->startup_loading = NULL;
    }

    furi_record_close(RECORD_GUI);
    instance->gui = NULL;

    instance->notifications = NULL;

    iso14443_3a_free(instance->iso14443_3a_edit_data);
    furi_string_free(instance->file_path);
    furi_string_free(instance->file_name);

    free(instance);
}

void nfc_text_store_set(NfcApp* nfc, const char* text, ...) {
    va_list args;
    va_start(args, text);

    vsnprintf(nfc->text_store, sizeof(nfc->text_store), text, args);

    va_end(args);
}

void nfc_text_store_clear(NfcApp* nfc) {
    memset(nfc->text_store, 0, sizeof(nfc->text_store));
}

void nfc_blink_read_start(NfcApp* nfc) {
    notification_message(nfc->notifications, &sequence_blink_start_yellow);
}

void nfc_blink_emulate_start(NfcApp* nfc) {
    notification_message(nfc->notifications, &sequence_blink_start_magenta);
}

void nfc_blink_detect_start(NfcApp* nfc) {
    notification_message(nfc->notifications, &sequence_blink_start_cyan);
}

void nfc_blink_stop(NfcApp* nfc) {
    notification_message(nfc->notifications, &sequence_blink_stop);
}

void nfc_make_app_folders(NfcApp* instance) {
    furi_assert(instance);

    if(!storage_simply_mkdir(instance->storage, NFC_APP_FOLDER)) {
        dialog_message_show_storage_error(instance->dialogs, "Cannot create\napp folder");
    }
}

bool nfc_save_file(NfcApp* instance, FuriString* path) {
    furi_assert(instance);
    furi_assert(path);

    bool result = nfc_device_save(instance->nfc_device, furi_string_get_cstr(instance->file_path));

    if(!result) {
        dialog_message_show_storage_error(instance->dialogs, "Cannot save\nkey file");
    }

    return result;
}

static bool nfc_set_shadow_file_path(FuriString* file_path, FuriString* shadow_file_path) {
    furi_assert(file_path);
    furi_assert(shadow_file_path);

    bool shadow_file_path_set = false;
    if(furi_string_end_with(file_path, NFC_APP_SHADOW_EXTENSION)) {
        furi_string_set(shadow_file_path, file_path);
        shadow_file_path_set = true;
    } else if(furi_string_end_with(file_path, NFC_APP_EXTENSION)) {
        size_t path_len = furi_string_size(file_path);

        furi_string_set_n(shadow_file_path, file_path, 0, path_len - 4);
        furi_string_cat_printf(shadow_file_path, "%s", NFC_APP_SHADOW_EXTENSION);
        shadow_file_path_set = true;
    }

    return shadow_file_path_set;
}

static bool nfc_has_shadow_file_internal(NfcApp* instance, FuriString* path) {
    furi_assert(path);

    bool has_shadow_file = false;
    FuriString* shadow_file_path = furi_string_alloc();
    do {
        if(furi_string_empty(path)) break;
        if(!nfc_set_shadow_file_path(path, shadow_file_path)) break;
        has_shadow_file =
            storage_common_exists(instance->storage, furi_string_get_cstr(shadow_file_path));
    } while(false);

    furi_string_free(shadow_file_path);

    return has_shadow_file;
}

bool nfc_has_shadow_file(NfcApp* instance) {
    furi_assert(instance);

    return nfc_has_shadow_file_internal(instance, instance->file_path);
}

static bool nfc_save_internal(NfcApp* instance, const char* extension) {
    furi_assert(instance);
    furi_assert(extension);

    bool result = false;

    nfc_make_app_folders(instance);

    if(furi_string_end_with(instance->file_path, NFC_APP_EXTENSION) ||
       (furi_string_end_with(instance->file_path, NFC_APP_SHADOW_EXTENSION))) {
        size_t filename_start = furi_string_search_rchar(instance->file_path, '/');
        furi_string_left(instance->file_path, filename_start);
    }

    furi_string_cat_printf(
        instance->file_path, "/%s%s", furi_string_get_cstr(instance->file_name), extension);

    result = nfc_save_file(instance, instance->file_path);

    return result;
}

bool nfc_save_shadow_file(NfcApp* instance) {
    furi_assert(instance);

    return nfc_save_internal(instance, NFC_APP_SHADOW_EXTENSION);
}

bool nfc_save(NfcApp* instance) {
    furi_assert(instance);

    return nfc_save_internal(instance, NFC_APP_EXTENSION);
}

bool nfc_load_file(NfcApp* instance, FuriString* path, bool show_dialog) {
    furi_assert(instance);
    furi_assert(path);
    bool result = false;

    FuriString* load_path = furi_string_alloc();
    if(nfc_has_shadow_file_internal(instance, path)) {
        nfc_set_shadow_file_path(path, load_path);
    } else if(furi_string_end_with(path, NFC_APP_SHADOW_EXTENSION)) {
        size_t path_len = furi_string_size(path);
        furi_string_set_n(load_path, path, 0, path_len - 4);
        furi_string_cat_printf(load_path, "%s", NFC_APP_EXTENSION);
    } else {
        furi_string_set(load_path, path);
    }

    result = nfc_device_load(instance->nfc_device, furi_string_get_cstr(load_path));

    if(result) {
        path_extract_filename(load_path, instance->file_name, true);
    }

    if((!result) && (show_dialog)) {
        dialog_message_show_storage_error(instance->dialogs, "Cannot load\nkey file");
    }

    furi_string_free(load_path);

    return result;
}

bool nfc_delete(NfcApp* instance) {
    furi_assert(instance);

    if(nfc_has_shadow_file(instance)) {
        nfc_delete_shadow_file(instance);
    }

    if(furi_string_end_with_str(instance->file_path, NFC_APP_SHADOW_EXTENSION)) {
        size_t path_len = furi_string_size(instance->file_path);
        furi_string_replace_at(instance->file_path, path_len - 4, 4, NFC_APP_EXTENSION);
    }

    return storage_simply_remove(instance->storage, furi_string_get_cstr(instance->file_path));
}

bool nfc_delete_shadow_file(NfcApp* instance) {
    furi_assert(instance);

    FuriString* shadow_file_path = furi_string_alloc();

    bool result = nfc_set_shadow_file_path(instance->file_path, shadow_file_path) &&
                  storage_simply_remove(instance->storage, furi_string_get_cstr(shadow_file_path));

    furi_string_free(shadow_file_path);
    return result;
}

bool nfc_load_from_file_select(NfcApp* instance) {
    furi_assert(instance);

    DialogsFileBrowserOptions browser_options;
    dialog_file_browser_set_basic_options(&browser_options, NFC_APP_EXTENSION, &I_Nfc_10px);
    browser_options.base_path = NFC_APP_FOLDER;
    browser_options.hide_dot_files = true;

    bool success = false;
    do {

        if(!dialog_file_browser_show(
               instance->dialogs, instance->file_path, instance->file_path, &browser_options))
            break;

        nfc_show_loading_popup(instance, true);

        success = nfc_load_file(instance, instance->file_path, true);
        if(success) {
            nfc_protocol_support_preload(
                nfc_device_get_protocol(instance->nfc_device), instance);
        }

        nfc_supported_cards_load_cache(instance->nfc_supported_cards);
        nfc_show_loading_popup(instance, false);
    } while(!success);

    return success;
}

void nfc_show_loading_popup(void* context, bool show) {
    NfcApp* nfc = context;

    if(show) {

        furi_timer_set_thread_priority(FuriTimerThreadPriorityElevated);
        view_dispatcher_switch_to_view(nfc->view_dispatcher, NfcViewLoading);
    } else {

        furi_timer_set_thread_priority(FuriTimerThreadPriorityNormal);
    }
}

void nfc_append_filename_string_when_present(NfcApp* instance, FuriString* string) {
    furi_assert(instance);
    furi_assert(string);

    if(!furi_string_empty(instance->file_name)) {
        furi_string_cat_printf(string, "Name: %s\n", furi_string_get_cstr(instance->file_name));
    }
}

static bool nfc_is_hal_ready(void) {
    if(furi_hal_nfc_is_hal_ready() != FuriHalNfcErrorNone) {

        DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
        DialogMessage* message = dialog_message_alloc();
        dialog_message_set_header(message, "Error: NFC Chip Failed", 64, 0, AlignCenter, AlignTop);
        dialog_message_set_text(
            message, "Send error photo via\nsupport.flipper.net", 0, 63, AlignLeft, AlignBottom);

        dialog_message_show(dialogs, message);
        dialog_message_free(message);
        furi_record_close(RECORD_DIALOGS);
        return false;
    } else {
        return true;
    }
}

static void nfc_show_initial_scene_for_device(NfcApp* nfc) {
    NfcProtocol prot = nfc_device_get_protocol(nfc->nfc_device);
    uint32_t scene = nfc_protocol_support_has_feature(
                         prot, nfc, NfcProtocolFeatureEmulateFull | NfcProtocolFeatureEmulateUid) ?
                         NfcSceneEmulate :
                         NfcSceneSavedMenu;

    if(scene == NfcSceneSavedMenu) {
        nfc_show_loading_popup(nfc, true);
        nfc_supported_cards_load_cache(nfc->nfc_supported_cards);
        nfc_show_loading_popup(nfc, false);
    }
    scene_manager_next_scene(nfc->scene_manager, scene);
}

void nfc_app_run_external(NfcApp* nfc, const char* app_path) {
    furi_assert(nfc);
    furi_assert(app_path);

    Loader* loader = furi_record_open(RECORD_LOADER);

    loader_enqueue_launch(loader, app_path, NULL, LoaderDeferredLaunchFlagGui);

    FuriString* self_path = furi_string_alloc();
    furi_check(loader_get_application_launch_path(loader, self_path));

    loader_enqueue_launch(
        loader, furi_string_get_cstr(self_path), NULL, LoaderDeferredLaunchFlagGui);
    furi_string_free(self_path);

    furi_record_close(RECORD_LOADER);

    view_dispatcher_stop(nfc->view_dispatcher);
}

void nfc_run_mfkey_collector(NfcApp* nfc, const char* card_path) {
    furi_assert(nfc);

    File* marker = storage_file_alloc(nfc->storage);
    if(storage_file_open(marker, NFC_FOCUS_MENU_MARKER_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_close(marker);
    }
    storage_file_free(marker);

    Loader* loader = furi_record_open(RECORD_LOADER);

    loader_enqueue_launch(
        loader, NFC_MFKEY_COLLECTOR_APP_PATH, card_path, LoaderDeferredLaunchFlagGui);

    FuriString* self_path = furi_string_alloc();
    furi_check(loader_get_application_launch_path(loader, self_path));

    loader_enqueue_launch(
        loader, furi_string_get_cstr(self_path), NULL, LoaderDeferredLaunchFlagGui);
    furi_string_free(self_path);

    furi_record_close(RECORD_LOADER);

    view_dispatcher_stop(nfc->view_dispatcher);
}

void nfc_run_mfkey_collector_with_card(NfcApp* nfc) {
    furi_assert(nfc);
    nfc_device_save(nfc->nfc_device, NFC_MFKEY_COLLECTOR_TMP_CARD_PATH);
    nfc_run_mfkey_collector(nfc, NFC_MFKEY_COLLECTOR_TMP_CARD_PATH);
}

void nfc_run_mifare_fuzzer(NfcApp* nfc) {
    furi_assert(nfc);

    File* marker = storage_file_alloc(nfc->storage);
    if(storage_file_open(marker, NFC_FOCUS_FUZZER_MARKER_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_close(marker);
    }
    storage_file_free(marker);

    Loader* loader = furi_record_open(RECORD_LOADER);

    loader_enqueue_launch(loader, NFC_MIFARE_FUZZER_APP_PATH, NULL, LoaderDeferredLaunchFlagGui);

    FuriString* self_path = furi_string_alloc();
    furi_check(loader_get_application_launch_path(loader, self_path));

    loader_enqueue_launch(
        loader, furi_string_get_cstr(self_path), NULL, LoaderDeferredLaunchFlagGui);
    furi_string_free(self_path);

    furi_record_close(RECORD_LOADER);

    view_dispatcher_stop(nfc->view_dispatcher);
}

int32_t nfc_app(void* p) {
    if(!nfc_is_hal_ready()) return 0;

    NfcApp* nfc = nfc_app_alloc();
    const char* args = p;

    if(args && strlen(args)) {
        if(sscanf(args, "RPC %p", &nfc->rpc_ctx) == 1) {
            rpc_system_app_set_callback(nfc->rpc_ctx, nfc_app_rpc_command_callback, nfc);
            rpc_system_app_send_started(nfc->rpc_ctx);
            view_dispatcher_attach_to_gui(
                nfc->view_dispatcher, nfc->gui, ViewDispatcherTypeDesktop);
            scene_manager_next_scene(nfc->scene_manager, NfcSceneRpc);
        } else {
            view_dispatcher_attach_to_gui(
                nfc->view_dispatcher, nfc->gui, ViewDispatcherTypeFullscreen);

            furi_string_set(nfc->file_path, args);
            if(nfc_load_file(nfc, nfc->file_path, true)) {
                nfc_show_initial_scene_for_device(nfc);
            } else {
                view_dispatcher_stop(nfc->view_dispatcher);
            }
        }
    } else {
        view_dispatcher_attach_to_gui(
            nfc->view_dispatcher, nfc->gui, ViewDispatcherTypeFullscreen);
        scene_manager_next_scene(nfc->scene_manager, NfcSceneStart);
    }

    view_dispatcher_run(nfc->view_dispatcher);

    nfc_app_free(nfc);

    return 0;
}
