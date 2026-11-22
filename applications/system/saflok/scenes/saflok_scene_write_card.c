#include "../saflok_app_i.h"
#include <gui/elements.h>
#include <gui/modules/widget.h>
#include "assets_icons.h"
#include <notification/notification.h>
#include <nfc/protocols/mf_classic/mf_classic_poller.h>
#include "../helpers/mf_classic_key_cache.h"

#define TAG "SaflokSceneWriteCard"

enum {
    SaflokSceneMfClassicWriteInitialStateCardSearch,
    SaflokSceneMfClassicWriteInitialStateCardFound,
};

static NfcCommand saflok_write_card_worker_callback(NfcGenericEvent event, void* context) {
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

static void saflok_scene_write_card_setup_view(SaflokApp* app) {
    Popup* popup = app->popup;
    popup_reset(popup);
    uint32_t state =
        scene_manager_get_scene_state(app->scene_manager, SaflokSceneMfClassicWriteInitial);

    if(state == SaflokSceneMfClassicWriteInitialStateCardSearch) {
        popup_set_text(
            app->popup, "Use the source\ncard only", 128, 32, AlignRight, AlignCenter);
        popup_set_icon(app->popup, 0, 8, &I_NFC_manual_60x50);
    } else {
        popup_set_header(popup, "Updating\nDon't move...", 52, 32, AlignLeft, AlignCenter);
        popup_set_icon(popup, 12, 23, &A_Loading_24);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewPopup);
}

void saflok_scene_write_card_on_enter(void* context) {
    SaflokApp* app = context;
    (void)0; /* dolphin_deed trimmed (Fox has no dolphin service) */

    SaflokMfClassicData* mfc_data = (SaflokMfClassicData*)nfc_device_get_data(app->saflok_device, NfcProtocolMfClassic);
    mfc_data->type = MfClassicType1k;
    mf_classic_key_cache_load_from_data(app->mfc_key_cache, (MfClassicData*)mfc_data);

    scene_manager_set_scene_state(app->scene_manager, SaflokSceneMfClassicWriteInitial, SaflokSceneMfClassicWriteInitialStateCardSearch);
    saflok_scene_write_card_setup_view(app);

    app->poller = nfc_poller_alloc(app->nfc, NfcProtocolMfClassic);
    nfc_poller_start(app->poller, saflok_write_card_worker_callback, app);

    notification_message(app->notifications, &sequence_blink_start_magenta);
}

bool saflok_scene_write_card_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SaflokCustomEventCardDetected) {
            scene_manager_set_scene_state(
                app->scene_manager,
                SaflokSceneMfClassicWriteInitial,
                SaflokSceneMfClassicWriteInitialStateCardFound);
            saflok_scene_write_card_setup_view(app);
            consumed = true;
        } else if(event.event == SaflokCustomEventCardLost) {
            scene_manager_set_scene_state(
                app->scene_manager,
                SaflokSceneMfClassicWriteInitial,
                SaflokSceneMfClassicWriteInitialStateCardSearch);
            saflok_scene_write_card_setup_view(app);
            consumed = true;
        } else if(event.event == SaflokCustomEventWrongCard) {
            scene_manager_next_scene(app->scene_manager, SaflokSceneMfClassicWriteInitialWrongCard);
            consumed = true;
        } else if(event.event == SaflokCustomEventWorkerExit) {
            if(saflok_save_shadow_file(app)) {
                scene_manager_next_scene(
                    app->scene_manager, SaflokSceneMfClassicWriteInitialSuccess);
            } else {
                scene_manager_next_scene(
                    app->scene_manager, SaflokSceneMfClassicWriteInitialWrongCard);
                consumed = true;
            }
        }
    }

    return consumed;
}

void saflok_scene_write_card_on_exit(void* context) {
    SaflokApp* app = context;

    if(app->poller) {
        nfc_poller_stop(app->poller);
        nfc_poller_free(app->poller);
        app->poller = NULL;
    }

    scene_manager_set_scene_state(
        app->scene_manager,
        SaflokSceneMfClassicWriteInitial,
        SaflokSceneMfClassicWriteInitialStateCardSearch);

    popup_reset(app->popup);

    notification_message(app->notifications, &sequence_blink_stop);
}
