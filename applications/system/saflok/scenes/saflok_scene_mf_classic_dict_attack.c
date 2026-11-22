#include "../saflok_app_i.h"

#include <bit_lib/bit_lib.h>

#undef TAG
#define TAG "SaflokMfClassicDictAttack"


typedef enum {
    DictAttackStateCUIDDictInProgress,
    DictAttackStateUserDictInProgress,
    DictAttackStateSystemDictInProgress,
} DictAttackState;

NfcCommand saflok_dict_attack_worker_callback(NfcGenericEvent event, void* context) {
    furi_assert(context);
    furi_assert(event.event_data);
    furi_assert(event.instance);
    furi_assert(event.protocol == NfcProtocolMfClassic);

    NfcCommand command = NfcCommandContinue;
    MfClassicPollerEvent* mfc_event = event.event_data;

    SaflokApp* app = context;
    if(mfc_event->type == MfClassicPollerEventTypeCardDetected) {
        app->saflok_dict_context.is_card_present = true;
        view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventCardDetected);
    } else if(mfc_event->type == MfClassicPollerEventTypeCardLost) {
        app->saflok_dict_context.is_card_present = false;
        view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventCardLost);
    } else if(mfc_event->type == MfClassicPollerEventTypeRequestMode) {
        SaflokMfClassicData* mfc_data = (SaflokMfClassicData*)nfc_device_get_data(app->saflok_device, NfcProtocolMfClassic);
        mfc_data->type = MfClassicType1k;
        mfc_event->data->poller_mode.mode = (app->saflok_dict_context.enhanced_dict) ?
                                                MfClassicPollerModeDictAttackEnhanced :
                                                MfClassicPollerModeDictAttackStandard;
        mfc_event->data->poller_mode.data = (MfClassicData*)mfc_data;
        app->saflok_dict_context.sectors_total =
            mf_classic_get_total_sectors_num(mfc_data->type);
        mf_classic_get_read_sectors_and_keys(
            (MfClassicData*)mfc_data,
            &app->saflok_dict_context.sectors_read,
            &app->saflok_dict_context.keys_found);
        view_dispatcher_send_custom_event(
            app->view_dispatcher, SaflokCustomEventDictAttackDataUpdate);
    } else if(mfc_event->type == MfClassicPollerEventTypeRequestKey) {
        MfClassicKey key = {};
        if(keys_dict_get_next_key(
               app->saflok_dict_context.dict, key.data, sizeof(MfClassicKey))) {
            mfc_event->data->key_request_data.key = key;
            mfc_event->data->key_request_data.key_provided = true;
            app->saflok_dict_context.dict_keys_current++;
            if(app->saflok_dict_context.dict_keys_current % 10 == 0) {
                view_dispatcher_send_custom_event(
                    app->view_dispatcher, SaflokCustomEventDictAttackDataUpdate);
            }
        } else {
            mfc_event->data->key_request_data.key_provided = false;
        }
    } else if(mfc_event->type == MfClassicPollerEventTypeDataUpdate) {
        MfClassicPollerEventDataUpdate* data_update = &mfc_event->data->data_update;
        app->saflok_dict_context.sectors_read = data_update->sectors_read;
        app->saflok_dict_context.keys_found = data_update->keys_found;
        app->saflok_dict_context.current_sector = data_update->current_sector;
        app->saflok_dict_context.nested_phase = data_update->nested_phase;
        app->saflok_dict_context.prng_type = data_update->prng_type;
        app->saflok_dict_context.backdoor = data_update->backdoor;
        app->saflok_dict_context.nested_target_key = data_update->nested_target_key;
        app->saflok_dict_context.msb_count = data_update->msb_count;
        view_dispatcher_send_custom_event(
            app->view_dispatcher, SaflokCustomEventDictAttackDataUpdate);
    } else if(mfc_event->type == MfClassicPollerEventTypeNextSector) {
        keys_dict_rewind(app->saflok_dict_context.dict);
        app->saflok_dict_context.dict_keys_current = 0;
        app->saflok_dict_context.current_sector =
            mfc_event->data->next_sector_data.current_sector;
        view_dispatcher_send_custom_event(
            app->view_dispatcher, SaflokCustomEventDictAttackDataUpdate);
    } else if(mfc_event->type == MfClassicPollerEventTypeFoundKeyA) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, SaflokCustomEventDictAttackDataUpdate);
    } else if(mfc_event->type == MfClassicPollerEventTypeFoundKeyB) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, SaflokCustomEventDictAttackDataUpdate);
    } else if(mfc_event->type == MfClassicPollerEventTypeKeyAttackStart) {
        app->saflok_dict_context.key_attack_current_sector =
            mfc_event->data->key_attack_data.current_sector;
        app->saflok_dict_context.is_key_attack = true;
        view_dispatcher_send_custom_event(
            app->view_dispatcher, SaflokCustomEventDictAttackDataUpdate);
    } else if(mfc_event->type == MfClassicPollerEventTypeKeyAttackStop) {
        keys_dict_rewind(app->saflok_dict_context.dict);
        app->saflok_dict_context.is_key_attack = false;
        app->saflok_dict_context.dict_keys_current = 0;
        view_dispatcher_send_custom_event(
            app->view_dispatcher, SaflokCustomEventDictAttackDataUpdate);
    } else if(mfc_event->type == MfClassicPollerEventTypeSuccess) {
        SaflokMfClassicData* mfc_data = (SaflokMfClassicData*)nfc_poller_get_data(app->poller);
        mfc_data->type = MfClassicType1k;
        nfc_device_set_data(app->saflok_device, NfcProtocolMfClassic, (MfClassicData*)mfc_data);
        view_dispatcher_send_custom_event(
            app->view_dispatcher, SaflokCustomEventDictAttackComplete);
        command = NfcCommandStop;
    }

    return command;
}

void saflok_dict_attack_dict_attack_result_callback(DictAttackEvent event, void* context) {
    furi_assert(context);
    SaflokApp* app = context;

    if(event == DictAttackEventSkipPressed) {
        view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventDictAttackSkip);
    }
}

static void saflok_scene_mf_classic_dict_attack_update_view(SaflokApp* app) {
    SaflokMfClassicDictAttackContext* mfc_dict = &app->saflok_dict_context;

    if(mfc_dict->is_key_attack) {
        dict_attack_set_key_attack(app->dict_attack, mfc_dict->key_attack_current_sector);
    } else {
        dict_attack_reset_key_attack(app->dict_attack);
        dict_attack_set_sectors_total(app->dict_attack, mfc_dict->sectors_total);
        dict_attack_set_sectors_read(app->dict_attack, mfc_dict->sectors_read);
        dict_attack_set_keys_found(app->dict_attack, mfc_dict->keys_found);
        dict_attack_set_current_dict_key(app->dict_attack, mfc_dict->dict_keys_current);
        dict_attack_set_current_sector(app->dict_attack, mfc_dict->current_sector);
        dict_attack_set_nested_phase(app->dict_attack, mfc_dict->nested_phase);
        dict_attack_set_prng_type(app->dict_attack, mfc_dict->prng_type);
        dict_attack_set_backdoor(app->dict_attack, mfc_dict->backdoor);
        dict_attack_set_nested_target_key(app->dict_attack, mfc_dict->nested_target_key);
        dict_attack_set_msb_count(app->dict_attack, mfc_dict->msb_count);
    }
}

static void saflok_scene_mf_classic_dict_attack_prepare_view(SaflokApp* app) {
    uint32_t state =
        scene_manager_get_scene_state(app->scene_manager, SaflokSceneMfClassicDictAttack);
    if(state == DictAttackStateCUIDDictInProgress) {
        size_t cuid_len = 0;
        const uint8_t* cuid = nfc_device_get_uid(app->saflok_device, &cuid_len);
        FuriString* cuid_dict_path = furi_string_alloc_printf(
            "%s/mf_classic_dict_%08lx.nfc",
            EXT_PATH("nfc/assets"),
            (uint32_t)bit_lib_bytes_to_num_be(cuid + (cuid_len - 4), 4));

        do {
            if(!keys_dict_check_presence(furi_string_get_cstr(cuid_dict_path))) {
                state = DictAttackStateUserDictInProgress;
                break;
            }

            app->saflok_dict_context.dict = keys_dict_alloc(
                furi_string_get_cstr(cuid_dict_path),
                KeysDictModeOpenExisting,
                sizeof(MfClassicKey));

            if(keys_dict_get_total_keys(app->saflok_dict_context.dict) == 0) {
                keys_dict_free(app->saflok_dict_context.dict);
                state = DictAttackStateUserDictInProgress;
                break;
            }

            dict_attack_set_header(app->dict_attack, "MF Classic CUID Dictionary");
        } while(false);

        furi_string_free(cuid_dict_path);
    }
    if(state == DictAttackStateUserDictInProgress) {
        do {
            app->saflok_dict_context.enhanced_dict = true;

            if(keys_dict_check_presence(SAFLOK_APP_MF_CLASSIC_DICT_SYSTEM_NESTED_PATH)) {
                storage_common_remove(
                    app->storage, SAFLOK_APP_MF_CLASSIC_DICT_SYSTEM_NESTED_PATH);
            }
            if(keys_dict_check_presence(SAFLOK_APP_MF_CLASSIC_DICT_SYSTEM_PATH)) {
                storage_common_copy(
                    app->storage,
                    SAFLOK_APP_MF_CLASSIC_DICT_SYSTEM_PATH,
                    SAFLOK_APP_MF_CLASSIC_DICT_SYSTEM_NESTED_PATH);
            }

            if(!keys_dict_check_presence(SAFLOK_APP_MF_CLASSIC_DICT_USER_PATH)) {
                state = DictAttackStateSystemDictInProgress;
                break;
            }

            if(keys_dict_check_presence(SAFLOK_APP_MF_CLASSIC_DICT_USER_NESTED_PATH)) {
                storage_common_remove(app->storage, SAFLOK_APP_MF_CLASSIC_DICT_USER_NESTED_PATH);
            }
            storage_common_copy(
                app->storage,
                SAFLOK_APP_MF_CLASSIC_DICT_USER_PATH,
                SAFLOK_APP_MF_CLASSIC_DICT_USER_NESTED_PATH);

            app->saflok_dict_context.dict = keys_dict_alloc(
                SAFLOK_APP_MF_CLASSIC_DICT_USER_PATH, KeysDictModeOpenAlways, sizeof(MfClassicKey));
            if(keys_dict_get_total_keys(app->saflok_dict_context.dict) == 0) {
                keys_dict_free(app->saflok_dict_context.dict);
                state = DictAttackStateSystemDictInProgress;
                break;
            }

            dict_attack_set_header(app->dict_attack, "MF Classic User Dictionary");
        } while(false);
    }
    if(state == DictAttackStateSystemDictInProgress) {
        app->saflok_dict_context.dict = keys_dict_alloc(
            SAFLOK_APP_MF_CLASSIC_DICT_SYSTEM_PATH, KeysDictModeOpenExisting, sizeof(MfClassicKey));
        dict_attack_set_header(app->dict_attack, "MF Classic System Dictionary");
    }

    app->saflok_dict_context.dict_keys_total =
        keys_dict_get_total_keys(app->saflok_dict_context.dict);
    dict_attack_set_total_dict_keys(
        app->dict_attack, app->saflok_dict_context.dict_keys_total);
    app->saflok_dict_context.dict_keys_current = 0;

    dict_attack_set_callback(
        app->dict_attack, saflok_dict_attack_dict_attack_result_callback, app);
    saflok_scene_mf_classic_dict_attack_update_view(app);

    scene_manager_set_scene_state(app->scene_manager, SaflokSceneMfClassicDictAttack, state);
}

void saflok_scene_mf_classic_dict_attack_on_enter(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_enter");

    scene_manager_set_scene_state(
        app->scene_manager, SaflokSceneMfClassicDictAttack, DictAttackStateCUIDDictInProgress);
    saflok_scene_mf_classic_dict_attack_prepare_view(app);
    dict_attack_set_card_state(app->dict_attack, true);
    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewDictAttack);
    saflok_blink_read_start(app);

    app->poller = nfc_poller_alloc(app->nfc, NfcProtocolMfClassic);
    nfc_poller_start(app->poller, saflok_dict_attack_worker_callback, app);
}

static void saflok_scene_mf_classic_dict_attack_notify_read(SaflokApp* app) {
    SaflokMfClassicData* mfc_data = (SaflokMfClassicData*)nfc_poller_get_data(app->poller);
    mfc_data->type = MfClassicType1k;
    bool is_card_fully_read = mf_classic_is_card_read((MfClassicData*)mfc_data);
    if(is_card_fully_read) {
        notification_message(app->notifications, &sequence_success);
    } else {
        notification_message(app->notifications, &sequence_semi_success);
    }
}

bool saflok_scene_mf_classic_dict_attack_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    
    FURI_LOG_D(TAG, "on_event");
    
    bool consumed = false;

    uint32_t state =
        scene_manager_get_scene_state(app->scene_manager, SaflokSceneMfClassicDictAttack);
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SaflokCustomEventDictAttackComplete) {
            bool ran_nested_dict = app->saflok_dict_context.nested_phase !=
                                   MfClassicNestedPhaseNone;
            if(state == DictAttackStateCUIDDictInProgress) {
                nfc_poller_stop(app->poller);
                nfc_poller_free(app->poller);
                keys_dict_free(app->saflok_dict_context.dict);
                scene_manager_set_scene_state(
                    app->scene_manager,
                    SaflokSceneMfClassicDictAttack,
                    DictAttackStateUserDictInProgress);
                saflok_scene_mf_classic_dict_attack_prepare_view(app);
                app->poller = nfc_poller_alloc(app->nfc, NfcProtocolMfClassic);
                nfc_poller_start(app->poller, saflok_dict_attack_worker_callback, app);
                consumed = true;
            } else if(state == DictAttackStateUserDictInProgress && !(ran_nested_dict)) {
                nfc_poller_stop(app->poller);
                nfc_poller_free(app->poller);
                keys_dict_free(app->saflok_dict_context.dict);
                scene_manager_set_scene_state(
                    app->scene_manager,
                    SaflokSceneMfClassicDictAttack,
                    DictAttackStateSystemDictInProgress);
                saflok_scene_mf_classic_dict_attack_prepare_view(app);
                app->poller = nfc_poller_alloc(app->nfc, NfcProtocolMfClassic);
                nfc_poller_start(app->poller, saflok_dict_attack_worker_callback, app);
                consumed = true;
            } else {
                saflok_scene_mf_classic_dict_attack_notify_read(app);
                scene_manager_next_scene(app->scene_manager, SaflokSceneReadSuccess);
                (void)0; /* dolphin_deed trimmed (Fox has no dolphin service) */
                consumed = true;
            }
        } else if(event.event == SaflokCustomEventCardDetected) {
            dict_attack_set_card_state(app->dict_attack, true);
            consumed = true;
        } else if(event.event == SaflokCustomEventCardLost) {
            dict_attack_set_card_state(app->dict_attack, false);
            consumed = true;
        } else if(event.event == SaflokCustomEventDictAttackDataUpdate) {
            saflok_scene_mf_classic_dict_attack_update_view(app);
        } else if(event.event == SaflokCustomEventDictAttackSkip) {
            SaflokMfClassicData* mfc_data = (SaflokMfClassicData*)nfc_poller_get_data(app->poller);
            mfc_data->type = MfClassicType1k;
            nfc_device_set_data(app->saflok_device, NfcProtocolMfClassic, (MfClassicData*)mfc_data);
            bool ran_nested_dict = app->saflok_dict_context.nested_phase !=
                                   MfClassicNestedPhaseNone;
            if(state == DictAttackStateCUIDDictInProgress) {
                if(app->saflok_dict_context.is_card_present) {
                    nfc_poller_stop(app->poller);
                    nfc_poller_free(app->poller);
                    keys_dict_free(app->saflok_dict_context.dict);
                    scene_manager_set_scene_state(
                        app->scene_manager,
                        SaflokSceneMfClassicDictAttack,
                        DictAttackStateUserDictInProgress);
                    saflok_scene_mf_classic_dict_attack_prepare_view(app);
                    app->poller = nfc_poller_alloc(app->nfc, NfcProtocolMfClassic);
                    nfc_poller_start(app->poller, saflok_dict_attack_worker_callback, app);
                } else {
                    saflok_scene_mf_classic_dict_attack_notify_read(app);
                    scene_manager_next_scene(app->scene_manager, SaflokSceneReadSuccess);
                    (void)0; /* dolphin_deed trimmed (Fox has no dolphin service) */
                }
                consumed = true;
            } else if(state == DictAttackStateUserDictInProgress && !(ran_nested_dict)) {
                if(app->saflok_dict_context.is_card_present) {
                    nfc_poller_stop(app->poller);
                    nfc_poller_free(app->poller);
                    keys_dict_free(app->saflok_dict_context.dict);
                    scene_manager_set_scene_state(
                        app->scene_manager,
                        SaflokSceneMfClassicDictAttack,
                        DictAttackStateSystemDictInProgress);
                    saflok_scene_mf_classic_dict_attack_prepare_view(app);
                    app->poller = nfc_poller_alloc(app->nfc, NfcProtocolMfClassic);
                    nfc_poller_start(app->poller, saflok_dict_attack_worker_callback, app);
                } else {
                    saflok_scene_mf_classic_dict_attack_notify_read(app);
                    scene_manager_next_scene(app->scene_manager, SaflokSceneReadSuccess);
                    (void)0; /* dolphin_deed trimmed (Fox has no dolphin service) */
                }
                consumed = true;
            } else {
                saflok_scene_mf_classic_dict_attack_notify_read(app);
                scene_manager_next_scene(app->scene_manager, SaflokSceneReadSuccess);
                (void)0; /* dolphin_deed trimmed (Fox has no dolphin service) */
                consumed = true;
            }
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        scene_manager_next_scene(app->scene_manager, SaflokSceneStart);
        consumed = true;
    }
    return consumed;
}

void saflok_scene_mf_classic_dict_attack_on_exit(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_exit");
    
    nfc_poller_stop(app->poller);
    nfc_poller_free(app->poller);

    dict_attack_reset(app->dict_attack);
    scene_manager_set_scene_state(
        app->scene_manager, SaflokSceneMfClassicDictAttack, DictAttackStateCUIDDictInProgress);

    keys_dict_free(app->saflok_dict_context.dict);

    app->saflok_dict_context.current_sector = 0;
    app->saflok_dict_context.sectors_total = 0;
    app->saflok_dict_context.sectors_read = 0;
    app->saflok_dict_context.keys_found = 0;
    app->saflok_dict_context.dict_keys_total = 0;
    app->saflok_dict_context.dict_keys_current = 0;
    app->saflok_dict_context.is_key_attack = false;
    app->saflok_dict_context.key_attack_current_sector = 0;
    app->saflok_dict_context.is_card_present = false;
    app->saflok_dict_context.nested_phase = MfClassicNestedPhaseNone;
    app->saflok_dict_context.prng_type = MfClassicPrngTypeUnknown;
    app->saflok_dict_context.backdoor = MfClassicBackdoorUnknown;
    app->saflok_dict_context.nested_target_key = 0;
    app->saflok_dict_context.msb_count = 0;
    app->saflok_dict_context.enhanced_dict = false;

    if(keys_dict_check_presence(SAFLOK_APP_MF_CLASSIC_DICT_USER_NESTED_PATH)) {
        storage_common_remove(app->storage, SAFLOK_APP_MF_CLASSIC_DICT_USER_NESTED_PATH);
    }
    if(keys_dict_check_presence(SAFLOK_APP_MF_CLASSIC_DICT_SYSTEM_NESTED_PATH)) {
        storage_common_remove(app->storage, SAFLOK_APP_MF_CLASSIC_DICT_SYSTEM_NESTED_PATH);
    }

    saflok_blink_stop(app);
}
