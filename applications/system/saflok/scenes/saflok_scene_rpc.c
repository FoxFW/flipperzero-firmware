#include "../saflok_app_i.h"

#include "../helpers/protocol_support/saflok_protocol_support_defs.h"
#include "../helpers/protocol_support/saflok_protocol_support_gui_common.h"

#undef TAG
#define TAG "SaflokSceneRpc"

static void saflok_scene_rpc_stop_listener(SaflokApp* app) {
    nfc_listener_stop(app->listener);

    const NfcProtocol protocol = nfc_device_get_protocol(app->saflok_device);

    if(protocol == nfc_listener_get_protocol(app->listener)) {
        const NfcDeviceData* data = nfc_listener_get_data(app->listener, protocol);

        if(!nfc_device_is_equal_data(app->saflok_device, protocol, data)) {
            nfc_device_set_data(app->saflok_device, protocol, data);
            saflok_save_shadow_file(app);
        }
    }

    nfc_listener_free(app->listener);
}

static void saflok_scene_rpc_setup_ui_and_emulate(SaflokApp* app) {
    saflok_text_store_set(app, "emulating\n%s", furi_string_get_cstr(app->file_name));

    popup_set_header(app->popup, "NFC", 89, 42, AlignCenter, AlignBottom);
    popup_set_text(app->popup, app->text_store, 89, 44, AlignCenter, AlignTop);
    popup_set_icon(app->popup, 0, 12, &I_RFIDDolphinSend_97x61);

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewPopup);

    notification_message(app->notifications, &sequence_display_backlight_on);
    saflok_blink_emulate_start(app);

    SaflokMfClassicData* data = (SaflokMfClassicData*)nfc_device_get_data(app->saflok_device, NfcProtocolMfClassic);
    data->type = MfClassicType1k;
    app->listener = nfc_listener_alloc(app->nfc, NfcProtocolMfClassic, data);
    nfc_listener_start(app->listener, NULL, NULL);

    app->rpc_state = SaflokRpcStateEmulating;
}

void saflok_scene_rpc_on_enter(void* context) { 
    FURI_LOG_D(TAG, "on_enter");
    furi_assert(context);

    SaflokApp* app = context;
    UNUSED(app);
}

bool saflok_scene_rpc_on_event(void* context, SceneManagerEvent event) { 
    FURI_LOG_D(TAG, "on_event");
    furi_assert(context);

    SaflokApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SaflokCustomEventRpcLoadFile) {
            bool success = false;
            if(app->rpc_state == SaflokRpcStateIdle) {
                if(saflok_load_file(app, app->file_path, false)) {
                    saflok_scene_rpc_setup_ui_and_emulate(app);
                    success = true;
                } else {
                    rpc_system_app_set_error_code(app->rpc_ctx, RpcAppSystemErrorCodeParseFile);
                    rpc_system_app_set_error_text(app->rpc_ctx, "Cannot load key file");
                }
            }
            rpc_system_app_confirm(app->rpc_ctx, success);
        } else if(event.event == SaflokCustomEventRpcExit) {
            rpc_system_app_confirm(app->rpc_ctx, true);
            scene_manager_stop(app->scene_manager);
            view_dispatcher_stop(app->view_dispatcher);
        } else if(event.event == SaflokCustomEventRpcSessionClose) {
            scene_manager_stop(app->scene_manager);
            view_dispatcher_stop(app->view_dispatcher);
        }
        consumed = true;
    }

    return consumed;
}

void saflok_scene_rpc_on_exit(void* context) {
    FURI_LOG_D(TAG, "on_exit");
    furi_assert(context);

    SaflokApp* app = context;

    if(app->rpc_state == SaflokRpcStateEmulating) {
        saflok_scene_rpc_stop_listener(app);
    }

    popup_reset(app->popup);
    text_box_reset(app->text_box);
    furi_string_reset(app->text_box_store);

    saflok_blink_stop(app);
}
