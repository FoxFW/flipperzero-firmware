#include "../saflok_app_i.h"

#undef TAG
#define TAG "SaflokSceneDetect"

void saflok_scene_detect_scan_callback(NfcScannerEvent event, void* context) {
    furi_assert(context);

    SaflokApp* app = context;

    if(event.type == NfcScannerEventTypeDetected) {
        saflok_detected_protocols_set(
            app->detected_protocols, event.data.protocols, event.data.protocol_num);
        view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventWorkerExit);
    }
}

void saflok_scene_detect_on_enter(void* context) {
    SaflokApp* app = context;
    FURI_LOG_D(TAG, "on_enter");

    popup_reset(app->popup);
    popup_set_header(app->popup, "Reading", 97, 15, AlignCenter, AlignTop);
    popup_set_text(
        app->popup, "Hold card next\nto Flipper's back", 94, 27, AlignCenter, AlignTop);
    popup_set_icon(app->popup, 0, 8, &I_NFC_manual_60x50);
    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewPopup);

    saflok_detected_protocols_reset(app->detected_protocols);

    app->scanner = nfc_scanner_alloc(app->nfc);
    nfc_scanner_start(app->scanner, saflok_scene_detect_scan_callback, app);

    saflok_blink_detect_start(app);
}

bool saflok_scene_detect_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    FURI_LOG_D(TAG, "on_event");
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SaflokCustomEventWorkerExit) {
            if(saflok_detected_protocols_get_num(app->detected_protocols) > 1) {
                notification_message(app->notifications, &sequence_single_vibro);
                scene_manager_next_scene(app->scene_manager, SaflokSceneSelectProtocol);
            } else {
                scene_manager_next_scene(app->scene_manager, SaflokSceneRead);
            }
            consumed = true;
        }
    }

    return consumed;
}

void saflok_scene_detect_on_exit(void* context) {
    SaflokApp* app = context;
    FURI_LOG_D(TAG, "on_exit");

    if(app->scanner) {
        nfc_scanner_stop(app->scanner);
        nfc_scanner_free(app->scanner);
        app->scanner = NULL;
    }
   
    popup_reset(app->popup);
    saflok_blink_stop(app);
}
