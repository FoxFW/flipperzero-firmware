#include "../saflok_app_i.h"

#include <nfc/protocols/mf_classic/mf_classic_listener.h>

#undef TAG
#define TAG "SaflokSceneMfClassicDetectReader"

#define NXP_MANUFACTURER_ID (0x04)

#define SAFLOK_SCENE_DETECT_READER_PAIR_NONCES_MAX        (10U)
#define SAFLOK_SCENE_DETECT_READER_WAIT_NONCES_TIMEOUT_MS (1000)

static const NotificationSequence sequence_detect_reader = {
    &message_green_255,
    &message_blue_255,
    &message_do_not_reset,
    NULL,
};

void saflok_scene_mf_classic_detect_reader_view_callback(void* context) {
    SaflokApp* app = context;

    view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventViewExit);
}

NfcCommand saflok_scene_mf_classic_detect_listener_callback(NfcGenericEvent event, void* context) {
    furi_assert(context);
    furi_assert(event.event_data);
    furi_assert(event.protocol == NfcProtocolMfClassic);

    SaflokApp* app = context;
    MfClassicListenerEvent* mfc_event = event.event_data;

    if(mfc_event->type == MfClassicListenerEventTypeAuthContextPartCollected) {
        MfClassicAuthContext* auth_ctx = &mfc_event->data->auth_context;
        mfkey32_logger_add_nonce(app->mfkey32_logger, auth_ctx);
        view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventWorkerUpdate);
    }

    return NfcCommandContinue;
}

void saflok_scene_mf_classic_timer_callback(void* context) {
    SaflokApp* app = context;

    view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventTimerExpired);
}

void saflok_scene_mf_classic_detect_reader_on_enter(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_enter");

    if(nfc_device_get_protocol(app->saflok_device) == NfcProtocolInvalid) {
        Iso14443_3aData iso3_data = {
            .uid_len = 7,
            .uid = {0},
            .atqa = {0x44, 0x00},
            .sak = 0x08,
        };
        iso3_data.uid[0] = NXP_MANUFACTURER_ID;
        furi_hal_random_fill_buf(&iso3_data.uid[1], iso3_data.uid_len - 1);
        SaflokMfClassicData* mfc_data = (SaflokMfClassicData*)mf_classic_alloc();
        mfc_data->type = MfClassicType1k;

        mfc_data->type = MfClassicType4k;
        iso14443_3a_copy(mfc_data->iso14443_3a_data, &iso3_data);
        nfc_device_set_data(app->saflok_device, NfcProtocolMfClassic, mfc_data);

        mf_classic_free((MfClassicData*)mfc_data);
    }

    const Iso14443_3aData* iso3_data =
        nfc_device_get_data(app->saflok_device, NfcProtocolIso14443_3a);
    uint32_t cuid = iso14443_3a_get_cuid(iso3_data);

    app->mfkey32_logger = mfkey32_logger_alloc(cuid);
    app->timer =
        furi_timer_alloc(saflok_scene_mf_classic_timer_callback, FuriTimerTypeOnce, app);

    detect_reader_set_nonces_max(app->detect_reader, SAFLOK_SCENE_DETECT_READER_PAIR_NONCES_MAX);
    detect_reader_set_callback(
        app->detect_reader, saflok_scene_mf_classic_detect_reader_view_callback, app);

    notification_message(app->notifications, &sequence_detect_reader);

    app->listener = nfc_listener_alloc(
        app->nfc,
        NfcProtocolMfClassic,
        nfc_device_get_data(app->saflok_device, NfcProtocolMfClassic));
    nfc_listener_start(
        app->listener, saflok_scene_mf_classic_detect_listener_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewDetectReader);
}

bool saflok_scene_mf_classic_detect_reader_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    
    FURI_LOG_D(TAG, "on_event");
    
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SaflokCustomEventWorkerUpdate) {
            furi_timer_stop(app->timer);
            notification_message(app->notifications, &sequence_blink_start_cyan);

            size_t nonces_pairs = 2 * mfkey32_logger_get_params_num(app->mfkey32_logger);
            detect_reader_set_state(app->detect_reader, DetectReaderStateReaderDetected);
            detect_reader_set_nonces_collected(app->detect_reader, nonces_pairs);
            if(nonces_pairs >= SAFLOK_SCENE_DETECT_READER_PAIR_NONCES_MAX) {
                if(app->listener) {
                    nfc_listener_stop(app->listener);
                    nfc_listener_free(app->listener);
                    app->listener = NULL;
                }
                detect_reader_set_state(app->detect_reader, DetectReaderStateDone);
                saflok_blink_stop(app);
                notification_message(app->notifications, &sequence_single_vibro);
                notification_message(app->notifications, &sequence_set_green_255);
            } else {
                furi_timer_start(app->timer, SAFLOK_SCENE_DETECT_READER_WAIT_NONCES_TIMEOUT_MS);
            }
            consumed = true;
        } else if(event.event == SaflokCustomEventTimerExpired) {
            detect_reader_set_state(app->detect_reader, DetectReaderStateReaderLost);
            saflok_blink_stop(app);
            notification_message(app->notifications, &sequence_detect_reader);
        } else if(event.event == SaflokCustomEventViewExit) {
            if(app->listener) {
                nfc_listener_stop(app->listener);
                nfc_listener_free(app->listener);
                app->listener = NULL;
            }
            scene_manager_next_scene(app->scene_manager, SaflokSceneMfClassicMfkeyNoncesInfo);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        if(app->listener) {
            nfc_listener_stop(app->listener);
            nfc_listener_free(app->listener);
            app->listener = NULL;
        }
        mfkey32_logger_free(app->mfkey32_logger);
        if(scene_manager_has_previous_scene(app->scene_manager, SaflokSceneSaveSuccess)) {
            consumed = scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, SaflokSceneStart);
        } else if(scene_manager_has_previous_scene(app->scene_manager, SaflokSceneReadSuccess)) {
            consumed = scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, SaflokSceneReadSuccess);
        }
    }

    return consumed;
}

void saflok_scene_mf_classic_detect_reader_on_exit(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_exit");
    
    detect_reader_reset(app->detect_reader);

    furi_timer_stop(app->timer);
    furi_timer_free(app->timer);

    saflok_blink_stop(app);
    notification_message(app->notifications, &sequence_reset_green);
}
