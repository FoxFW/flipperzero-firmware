#include "../saflok_app_i.h"

#include <nfc/protocols/mf_classic/mf_classic_poller.h>

#undef TAG
#define TAG "SaflokSceneMfClassicUpdateInitial"

enum {
    SaflokSceneMfClassicUpdateInitialStateCardSearch,
    SaflokSceneMfClassicUpdateInitialStateCardFound,
};

NfcCommand saflok_mf_classic_update_initial_worker_callback(NfcGenericEvent event, void* context) {
    furi_assert(context);
    furi_assert(event.event_data);
    furi_assert(event.protocol == NfcProtocolMfClassic);

    NfcCommand command = NfcCommandContinue;
    const MfClassicPollerEvent* mfc_event = event.event_data;
    SaflokApp* app = context;

    if(mfc_event->type == MfClassicPollerEventTypeCardDetected) {
        view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventCardDetected);
    } else if(mfc_event->type == MfClassicPollerEventTypeCardLost) {
        view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventCardLost);
    } else if(mfc_event->type == MfClassicPollerEventTypeRequestMode) {
        SaflokMfClassicData* updated_data = (SaflokMfClassicData*)nfc_poller_get_data(app->poller);
        updated_data->type = MfClassicType1k;
        SaflokMfClassicData* old_data = (SaflokMfClassicData*)nfc_device_get_data(app->saflok_device, NfcProtocolMfClassic);
        old_data->type = MfClassicType1k;
        if(iso14443_3a_is_equal(updated_data->iso14443_3a_data, old_data->iso14443_3a_data)) {
            mfc_event->data->poller_mode.mode = MfClassicPollerModeRead;
        } else {
            view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventWrongCard);
            command = NfcCommandStop;
        }
    } else if(mfc_event->type == MfClassicPollerEventTypeRequestReadSector) {
        uint8_t sector_num = 0;
        MfClassicKey key = {};
        MfClassicKeyType key_type = MfClassicKeyTypeA;
        if(mf_classic_key_cache_get_next_key(
               app->mfc_key_cache, &sector_num, &key, &key_type)) {
            mfc_event->data->read_sector_request_data.sector_num = sector_num;
            mfc_event->data->read_sector_request_data.key = key;
            mfc_event->data->read_sector_request_data.key_type = key_type;
            mfc_event->data->read_sector_request_data.key_provided = true;
        } else {
            mfc_event->data->read_sector_request_data.key_provided = false;
        }
    } else if(mfc_event->type == MfClassicPollerEventTypeSuccess) {
        SaflokMfClassicData* updated_data = (SaflokMfClassicData*)nfc_poller_get_data(app->poller);
        updated_data->type = MfClassicType1k;
        nfc_device_set_data(app->saflok_device, NfcProtocolMfClassic, updated_data);
        view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventWorkerExit);
        command = NfcCommandStop;
    }

    return command;
}

static void saflok_scene_mf_classic_update_initial_setup_view(SaflokApp* app) {
    Popup* popup = app->popup;
    popup_reset(popup);
    uint32_t state =
        scene_manager_get_scene_state(app->scene_manager, SaflokSceneMfClassicUpdateInitial);

    if(state == SaflokSceneMfClassicUpdateInitialStateCardSearch) {
        popup_set_text(
            app->popup, "Use the source\ncard only", 128, 32, AlignRight, AlignCenter);
        popup_set_icon(app->popup, 0, 8, &I_NFC_manual_60x50);
    } else {
        popup_set_header(popup, "Updating\nDon't move...", 52, 32, AlignLeft, AlignCenter);
        popup_set_icon(popup, 12, 23, &A_Loading_24);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewPopup);
}

void saflok_scene_mf_classic_update_initial_on_enter(void* context) {
    SaflokApp* app = context;
    
    FURI_LOG_D(TAG, "on_enter");

    (void)0; /* dolphin_deed trimmed (Fox has no dolphin service) */

    SaflokMfClassicData* mfc_data = (SaflokMfClassicData*)nfc_device_get_data(app->saflok_device, NfcProtocolMfClassic);
    mfc_data->type = MfClassicType1k;
    mf_classic_key_cache_load_from_data(app->mfc_key_cache, (MfClassicData*)mfc_data);

    scene_manager_set_scene_state(
        app->scene_manager,
        SaflokSceneMfClassicUpdateInitial,
        SaflokSceneMfClassicUpdateInitialStateCardSearch);
    saflok_scene_mf_classic_update_initial_setup_view(app);

    app->poller = nfc_poller_alloc(app->nfc, NfcProtocolMfClassic);
    nfc_poller_start(app->poller, saflok_mf_classic_update_initial_worker_callback, app);
    saflok_blink_emulate_start(app);
}

bool saflok_scene_mf_classic_update_initial_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    
    FURI_LOG_D(TAG, "on_event");
    
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SaflokCustomEventCardDetected) {
            scene_manager_set_scene_state(
                app->scene_manager,
                SaflokSceneMfClassicUpdateInitial,
                SaflokSceneMfClassicUpdateInitialStateCardFound);
            saflok_scene_mf_classic_update_initial_setup_view(app);
            consumed = true;
        } else if(event.event == SaflokCustomEventCardLost) {
            scene_manager_set_scene_state(
                app->scene_manager,
                SaflokSceneMfClassicUpdateInitial,
                SaflokSceneMfClassicUpdateInitialStateCardSearch);
            saflok_scene_mf_classic_update_initial_setup_view(app);
            consumed = true;
        } else if(event.event == SaflokCustomEventWrongCard) {
            scene_manager_next_scene(
                app->scene_manager, SaflokSceneMfClassicUpdateInitialWrongCard);
            consumed = true;
        } else if(event.event == SaflokCustomEventWorkerExit) {
            if(saflok_save_shadow_file(app)) {
                scene_manager_next_scene(
                    app->scene_manager, SaflokSceneMfClassicUpdateInitialSuccess);
            } else {
                scene_manager_next_scene(
                    app->scene_manager, SaflokSceneMfClassicUpdateInitialWrongCard);
                consumed = true;
            }
        }
    }

    return consumed;
}

void saflok_scene_mf_classic_update_initial_on_exit(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_exit");
    
    nfc_poller_stop(app->poller);
    nfc_poller_free(app->poller);

    scene_manager_set_scene_state(
        app->scene_manager,
        SaflokSceneMfClassicUpdateInitial,
        SaflokSceneMfClassicUpdateInitialStateCardSearch);
    popup_reset(app->popup);

    saflok_blink_stop(app);
}
