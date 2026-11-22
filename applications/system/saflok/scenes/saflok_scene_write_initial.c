#include "../saflok_app_i.h"

#include <nfc/protocols/mf_classic/mf_classic_poller.h>

enum {
    SaflokSceneWriteInitialStateCardSearch,
    SaflokSceneWriteInitialStateCardFound,
};

NfcCommand
    saflok_scene_write_initial_worker_callback(NfcGenericEvent event, void* context) {
    furi_assert(context);
    furi_assert(event.event_data);
    furi_assert(event.protocol == NfcProtocolMfClassic);

    NfcCommand command = NfcCommandContinue;
    SaflokApp* app = context;
    MfClassicPollerEvent* mfc_event = event.event_data;
    SaflokMfClassicData* write_data = (SaflokMfClassicData*)nfc_device_get_data(app->saflok_device, NfcProtocolMfClassic);
    write_data->type = MfClassicType1k;

    if(mfc_event->type == MfClassicPollerEventTypeCardDetected) {
        view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventCardDetected);
    } else if(mfc_event->type == MfClassicPollerEventTypeCardLost) {
        view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventCardLost);
    } else if(mfc_event->type == MfClassicPollerEventTypeRequestMode) {
        SaflokMfClassicData* tag_data = (SaflokMfClassicData*)nfc_poller_get_data(app->poller);
        tag_data->type = MfClassicType1k;
        if(iso14443_3a_is_equal(tag_data->iso14443_3a_data, write_data->iso14443_3a_data)) {
            mfc_event->data->poller_mode.mode = MfClassicPollerModeWrite;
        } else {
            view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventWrongCard);
            command = NfcCommandStop;
        }
    } else if(mfc_event->type == MfClassicPollerEventTypeRequestSectorTrailer) {
        uint8_t sector = mfc_event->data->sec_tr_data.sector_num;
        uint8_t sec_tr = mf_classic_get_sector_trailer_num_by_sector(sector);
        if(mf_classic_is_block_read((MfClassicData*)write_data, sec_tr)) {
            mfc_event->data->sec_tr_data.sector_trailer = write_data->block[sec_tr];
            mfc_event->data->sec_tr_data.sector_trailer_provided = true;
        } else {
            mfc_event->data->sec_tr_data.sector_trailer_provided = false;
        }
    } else if(mfc_event->type == MfClassicPollerEventTypeRequestWriteBlock) {
        uint8_t block_num = mfc_event->data->write_block_data.block_num;
        if(mf_classic_is_block_read((MfClassicData*)write_data, block_num)) {
            mfc_event->data->write_block_data.write_block = write_data->block[block_num];
            mfc_event->data->write_block_data.write_block_provided = true;
        } else {
            mfc_event->data->write_block_data.write_block_provided = false;
        }
    } else if(mfc_event->type == MfClassicPollerEventTypeSuccess) {
        view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventPollerSuccess);
        command = NfcCommandStop;
    } else if(mfc_event->type == MfClassicPollerEventTypeFail) {
        view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventPollerFailure);
        command = NfcCommandStop;
    }
    return command;
}

static void saflok_scene_write_initial_setup_view(SaflokApp* app) {
    Popup* popup = app->popup;
    popup_reset(popup);
    uint32_t state =
        scene_manager_get_scene_state(app->scene_manager, SaflokSceneWriteInitial);

    if(state == SaflokSceneWriteInitialStateCardSearch) {
        popup_set_header(app->popup, "Writing", 95, 20, AlignCenter, AlignCenter);
        popup_set_text(
            app->popup, "Use the source\ncard only", 95, 38, AlignCenter, AlignCenter);
        popup_set_icon(app->popup, 0, 8, &I_NFC_manual_60x50);
    } else {
        popup_set_header(popup, "Writing\nDon't move...", 52, 32, AlignLeft, AlignCenter);
        popup_set_icon(popup, 12, 23, &A_Loading_24);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewPopup);
}

void saflok_scene_write_initial_on_enter(void* context) {
    SaflokApp* app = context;
    (void)0; /* dolphin_deed trimmed (Fox has no dolphin service) */

    scene_manager_set_scene_state(
        app->scene_manager,
        SaflokSceneWriteInitial,
        SaflokSceneWriteInitialStateCardSearch);
    saflok_scene_write_initial_setup_view(app);

    app->poller = nfc_poller_alloc(app->nfc, NfcProtocolMfClassic);
    nfc_poller_start(
        app->poller, saflok_scene_write_initial_worker_callback, app);

    notification_message(app->notifications, &sequence_blink_start_magenta);
}

bool saflok_scene_write_initial_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SaflokCustomEventCardDetected) {
            scene_manager_set_scene_state(
                app->scene_manager,
                SaflokSceneWriteInitial,
                SaflokSceneWriteInitialStateCardFound);
            saflok_scene_write_initial_setup_view(app);
            consumed = true;
        } else if(event.event == SaflokCustomEventCardLost) {
            scene_manager_set_scene_state(
                app->scene_manager,
                SaflokSceneWriteInitial,
                SaflokSceneWriteInitialStateCardSearch);
            saflok_scene_write_initial_setup_view(app);
            consumed = true;
        } else if(event.event == SaflokCustomEventWrongCard) {
            scene_manager_next_scene(
                app->scene_manager, SaflokSceneWriteInitialWrongCard);
            consumed = true;
        } else if(event.event == SaflokCustomEventPollerSuccess) {
            scene_manager_next_scene(
                app->scene_manager, SaflokSceneWriteInitialSuccess);
            consumed = true;
        } else if(event.event == SaflokCustomEventPollerFailure) {
            scene_manager_next_scene(app->scene_manager, SaflokSceneWriteInitialFail);
            consumed = true;
        }
    }

    return consumed;
}

void saflok_scene_write_initial_on_exit(void* context) {
    SaflokApp* app = context;

    if(app->poller) {
        nfc_poller_stop(app->poller);
        nfc_poller_free(app->poller);
        app->poller = NULL;
    }

    scene_manager_set_scene_state(
        app->scene_manager,
        SaflokSceneWriteInitial,
        SaflokSceneWriteInitialStateCardSearch);
    popup_reset(app->popup);

    notification_message(app->notifications, &sequence_blink_stop);
}
