#include "../saflok_app_i.h"
#include "../helpers/saflok_encryption_helpers.h"

#include <nfc/protocols/mf_classic/mf_classic_poller.h>

#undef TAG
#define TAG "SaflokSceneMfClassicWriteInitial"

enum {
    SaflokSceneMfClassicWriteInitialStateCardSearch,
    SaflokSceneMfClassicWriteInitialStateCardFound,
};

NfcCommand
    saflok_scene_mf_classic_write_initial_worker_callback(NfcGenericEvent event, void* context) {
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
            uint8_t encrypted_data[SAFLOK_DATA_LENGTH] = {0};

            if (has_unsaved_changes(app)) {
                FURI_LOG_D(TAG, "Saving pending changes");
                save_changes(app);
            }

            if(!saflok_encrypt(app->saflok_data->packed_data, encrypted_data)) {
                FURI_LOG_E(TAG, "Failed to encrypt Saflok data");
                popup_set_header(app->popup, "Error", 64, 32, AlignCenter, AlignCenter);
                popup_set_text(app->popup, "Failed to encrypt data", 64, 44, AlignCenter, AlignCenter);
                popup_set_icon(app->popup, 0, 3, &I_Warning_30x23);
                popup_set_timeout(app->popup, 3000);
                view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewPopup);
                app->popup_visible = true;
            }

            SaflokMfClassicData* original_data = (SaflokMfClassicData*)nfc_device_get_data(app->saflok_device, NfcProtocolMfClassic);
            original_data->type = MfClassicType1k;
            if(!original_data) {
                FURI_LOG_E(TAG, "MfClassic data is NULL");
                popup_set_header(app->popup, "Error", 64, 32, AlignCenter, AlignCenter);
                popup_set_text(app->popup, "Card data not available", 64, 44, AlignCenter, AlignCenter);
                popup_set_icon(app->popup, 0, 3, &I_Warning_30x23);
                popup_set_timeout(app->popup, 3000);
                view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewPopup);
                app->popup_visible = true;
            }

            SaflokMfClassicData modified_data;
            memcpy(&modified_data, original_data, sizeof(SaflokMfClassicData));

            memcpy(modified_data.block[1].data, encrypted_data, 16);

            modified_data.block[2].data[0] = encrypted_data[16];

            nfc_device_set_data(app->saflok_device, NfcProtocolMfClassic, &modified_data);

            SaflokMfClassicData* updated_data = (SaflokMfClassicData*)nfc_device_get_data(app->saflok_device, NfcProtocolMfClassic);
            updated_data->type = MfClassicType1k;
            mf_classic_key_cache_load_from_data(app->mfc_key_cache, (MfClassicData*)updated_data);
            
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

static void saflok_scene_mf_classic_write_initial_setup_view(SaflokApp* app) {
    Popup* popup = app->popup;
    popup_reset(popup);
    uint32_t state =
        scene_manager_get_scene_state(app->scene_manager, SaflokSceneMfClassicWriteInitial);

    if(state == SaflokSceneMfClassicWriteInitialStateCardSearch) {
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

void saflok_scene_mf_classic_write_initial_on_enter(void* context) {
    SaflokApp* app = context;
    
    FURI_LOG_D(TAG, "on_enter");

    (void)0; /* dolphin_deed trimmed (Fox has no dolphin service) */

    scene_manager_set_scene_state(
        app->scene_manager,
        SaflokSceneMfClassicWriteInitial,
        SaflokSceneMfClassicWriteInitialStateCardSearch);
    saflok_scene_mf_classic_write_initial_setup_view(app);

    app->poller = nfc_poller_alloc(app->nfc, NfcProtocolMfClassic);
    nfc_poller_start(
        app->poller, saflok_scene_mf_classic_write_initial_worker_callback, app);

    saflok_blink_emulate_start(app);
}

bool saflok_scene_mf_classic_write_initial_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    
    FURI_LOG_D(TAG, "on_event");
    
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SaflokCustomEventCardDetected) {
            scene_manager_set_scene_state(
                app->scene_manager,
                SaflokSceneMfClassicWriteInitial,
                SaflokSceneMfClassicWriteInitialStateCardFound);
            saflok_scene_mf_classic_write_initial_setup_view(app);
            consumed = true;
        } else if(event.event == SaflokCustomEventCardLost) {
            scene_manager_set_scene_state(
                app->scene_manager,
                SaflokSceneMfClassicWriteInitial,
                SaflokSceneMfClassicWriteInitialStateCardSearch);
            saflok_scene_mf_classic_write_initial_setup_view(app);
            consumed = true;
        } else if(event.event == SaflokCustomEventWrongCard) {
            scene_manager_next_scene(
                app->scene_manager, SaflokSceneMfClassicWriteInitialWrongCard);
            consumed = true;
        } else if(event.event == SaflokCustomEventPollerSuccess) {
            scene_manager_next_scene(
                app->scene_manager, SaflokSceneMfClassicWriteInitialSuccess);
            consumed = true;
        } else if(event.event == SaflokCustomEventPollerFailure) {
            scene_manager_next_scene(app->scene_manager, SaflokSceneMfClassicWriteInitialFail);
            consumed = true;
        }
    }

    return consumed;
}

void saflok_scene_mf_classic_write_initial_on_exit(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_exit");
   
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

    saflok_blink_stop(app);
}
