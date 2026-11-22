#include "../saflok_app_i.h"

#include "../helpers/saflok_detected_protocols.h"
#include "../helpers/saflok_app_helpers.h"
#include "../helpers/saflok_encryption_helpers.h"
#include "../helpers/protocol_support/saflok_protocol_support_defs.h"
#include "../helpers/protocol_support/saflok_protocol_support_gui_common.h"

#undef TAG
#define TAG "SaflokSceneRead"

static NfcCommand saflok_scene_read_poller_callback(NfcGenericEvent event, void* context) {
    furi_assert(event.protocol == NfcProtocolMfClassic);

    SaflokApp* app = context;
    const MfClassicPollerEvent* mfc_event = event.event_data;
    NfcCommand command = NfcCommandContinue;

    if(mfc_event->type == MfClassicPollerEventTypeRequestMode) {
        nfc_device_set_data(app->saflok_device, NfcProtocolMfClassic, nfc_poller_get_data(app->poller));
        size_t uid_len = 0;
        const uint8_t* uid = nfc_device_get_uid(app->saflok_device, &uid_len);
        if(mf_classic_key_cache_load(app->mfc_key_cache, uid, uid_len)) {
            FURI_LOG_I(TAG, "Key cache found");
            mfc_event->data->poller_mode.mode = MfClassicPollerModeRead;
        } else {
            FURI_LOG_I(TAG, "Key cache not found");
            view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventPollerIncomplete);
            command = NfcCommandStop;
        }
    } else if(mfc_event->type == MfClassicPollerEventTypeRequestReadSector) {
        uint8_t sector_num = 0;
        MfClassicKey key = {};
        MfClassicKeyType key_type = MfClassicKeyTypeA;
        if(mf_classic_key_cache_get_next_key(app->mfc_key_cache, &sector_num, &key, &key_type)) {
            mfc_event->data->read_sector_request_data.sector_num = sector_num;
            mfc_event->data->read_sector_request_data.key = key;
            mfc_event->data->read_sector_request_data.key_type = key_type;
            mfc_event->data->read_sector_request_data.key_provided = true;
        } else {
            mfc_event->data->read_sector_request_data.key_provided = false;
        }
    } else if(mfc_event->type == MfClassicPollerEventTypeSuccess) {
        nfc_device_set_data(app->saflok_device, NfcProtocolMfClassic, nfc_poller_get_data(app->poller));
        SaflokMfClassicData* mfc_data = (SaflokMfClassicData*)nfc_device_get_data(app->saflok_device, NfcProtocolMfClassic);
        mfc_data->type = MfClassicType1k;
        mf_classic_copy((MfClassicData*)app->mf_classic_data, (MfClassicData*)mfc_data);

        uint8_t encrypted_data[SAFLOK_DATA_LENGTH];
        memcpy(encrypted_data, app->mf_classic_data->block[1].data, 16);
        encrypted_data[16] = app->mf_classic_data->block[2].data[0];

        if(decrypt_data(encrypted_data, app->saflok_data->packed_data)) {
            FURI_LOG_D(TAG, "Saflok Data decrypted into app->saflok_data->packed_data");
            if(saflok_unpack_data(app->saflok_data->packed_data, app)) {
                FURI_LOG_D(TAG, "Packed data unpacked into app->saflok_data");
            } else {
                FURI_LOG_E(TAG, "Failed to unpack Saflok data");
            }
        } else {
            FURI_LOG_E(TAG, "Failed to decrypt Saflok data");
        }

        app->nfc_mode = true;
        scene_manager_next_scene(app->scene_manager, SaflokSceneTopLevel);
        command = NfcCommandStop;
    }

    return command;
}

void saflok_scene_read_on_enter(void* context) {
    FURI_LOG_D(TAG, "on_enter");
    furi_assert(context);

    SaflokApp* app = context;
    popup_set_header(app->popup, "Don't move", 85, 27, AlignCenter, AlignTop);
    popup_set_icon(app->popup, 12, 23, &A_Loading_24);

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewPopup);

    const NfcProtocol protocol = saflok_detected_protocols_get_selected(app->detected_protocols);
    app->poller = nfc_poller_alloc(app->nfc, protocol);

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewPopup);

    mf_classic_key_cache_reset(app->mfc_key_cache);
    nfc_poller_start(app->poller, saflok_scene_read_poller_callback, app);

    saflok_blink_read_start(app);
}

bool saflok_scene_read_on_event(void* context, SceneManagerEvent event) {
    FURI_LOG_D(TAG, "on_event");
    furi_assert(context);
    SaflokApp* app = context;
    
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SaflokCustomEventPollerSuccess || 
           event.event == SaflokCustomEventPollerIncomplete) {
            nfc_poller_stop(app->poller);
            nfc_poller_free(app->poller);
            app->poller = NULL;
            notification_message(app->notifications, &sequence_success);
            scene_manager_next_scene(app->scene_manager, SaflokSceneTopLevel);
            (void)0; /* dolphin_deed trimmed (Fox has no dolphin service) */
            return true;
        } else if(event.event == SaflokCustomEventPollerFailure) {
            nfc_poller_stop(app->poller);
            nfc_poller_free(app->poller);
            app->poller = NULL;
            if(scene_manager_has_previous_scene(app->scene_manager, SaflokSceneDetect)) {
                scene_manager_search_and_switch_to_previous_scene(app->scene_manager, SaflokSceneDetect);
            }
            return true;
        } else if(event.event == SaflokCustomEventCardDetected) {
            scene_manager_next_scene(app->scene_manager, SaflokSceneMfClassicDictAttack);
            return true;
        }
    } 
    else if(event.type == SceneManagerEventTypeBack) {
        nfc_poller_stop(app->poller);
        nfc_poller_free(app->poller);
        app->poller = NULL;
        static const uint32_t possible_scenes[] = {SaflokSceneSelectProtocol, SaflokSceneStart};
        scene_manager_search_and_switch_to_previous_scene_one_of(
            app->scene_manager, possible_scenes, COUNT_OF(possible_scenes));
        return true;
    }
    
    return false;
}

void saflok_scene_read_on_exit(void* context) {
    FURI_LOG_D(TAG, "on_exit");
    furi_assert(context);

    SaflokApp* app = context;
    
    popup_reset(app->popup);
    saflok_blink_stop(app);
}
