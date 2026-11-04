#include "../subghz_i.h"
#include "../subghz_protocol_filter.h"
#include <lib/subghz/subghz_protocol_registry.h>
#include <string.h>
#include "../helpers/subghz_lowram_guard.h"
#include "../helpers/subghz_decode_plugin_loader.h"

#define TAG "SubGhzDecodeRaw"

#define SAMPLES_TO_READ_PER_TICK 400

/* Below this free heap a PC (qFlipper) is eating too much RAM for decode_raw's
 * file-worker + decode to run; refuse with a message instead of OOM-crashing.
 * decode_raw uses only the resident live decoders (no .fal load). */
#define SUBGHZ_DECODE_RAW_MIN_HEAP 18432u

static void subghz_scene_receiver_update_statusbar(void* context) {
    SubGhz* subghz = context;
    FuriString* history_stat_str = furi_string_alloc();
    if(!subghz_history_get_text_space_left(subghz->history, history_stat_str)) {
        FuriString* frequency_str = furi_string_alloc();
        FuriString* modulation_str = furi_string_alloc();

        subghz_txrx_get_frequency_and_modulation(
            subghz->txrx, frequency_str, modulation_str, false);

        subghz_view_receiver_add_data_statusbar(
            subghz->subghz_receiver,
            furi_string_get_cstr(frequency_str),
            furi_string_get_cstr(modulation_str),
            furi_string_get_cstr(history_stat_str),
            subghz_txrx_hopper_get_state(subghz->txrx) != SubGhzHopperStateOFF,
            READ_BIT(subghz->filter, SubGhzProtocolFlag_BinRAW) > 0);

        furi_string_free(frequency_str);
        furi_string_free(modulation_str);
    } else {
        subghz_view_receiver_add_data_statusbar(
            subghz->subghz_receiver,
            furi_string_get_cstr(history_stat_str),
            "",
            "",
            subghz_txrx_hopper_get_state(subghz->txrx) != SubGhzHopperStateOFF,
            READ_BIT(subghz->filter, SubGhzProtocolFlag_BinRAW) > 0);
    }
    furi_string_free(history_stat_str);
}

void subghz_scene_decode_raw_callback(SubGhzCustomEvent event, void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, event);
}

static void subghz_scene_add_to_history_callback(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    SubGhzRadioPreset preset = subghz_txrx_get_preset(subghz->txrx);

    bool identical_ping = false;
    {
        bool ping_notify = false;
        int16_t ident =
            subghz_history_find_identical(subghz->history, decoder_base, &preset, &ping_notify);
        if(ident >= 0) {
            identical_ping = true;
            subghz_view_receiver_flash_item(subghz->subghz_receiver, (uint16_t)ident);
            if(ping_notify) {
                subghz->state_notifications = SubGhzNotificationStateRxDone;
            }
        }
    }

    if(!identical_ping && subghz_history_add_to_history(
                                 subghz->history, decoder_base, &preset, SubGhzHistorySourceNone)) {
        subghz->state_notifications = SubGhzNotificationStateRxDone;

        subghz_view_receiver_add_item_to_menu(subghz->subghz_receiver);

        int16_t dedup_removed = subghz_history_dedup_last_fiat_hitag2(subghz->history);
        if(dedup_removed >= 0) {
            subghz_view_receiver_disable_draw_callback(subghz->subghz_receiver);
            subghz_view_receiver_delete_item(subghz->subghz_receiver, (uint16_t)dedup_removed);
            subghz_view_receiver_enable_draw_callback(subghz->subghz_receiver);
        }

        subghz_scene_receiver_update_statusbar(subghz);
    }
    if(decoder_base->protocol->name && !strcmp(decoder_base->protocol->name, "Hitag-2")) {
        subghz_receiver_reset_except_by_name(receiver, "Fiat V1");
    } else {
        subghz_receiver_reset(receiver);
    }
}

bool subghz_scene_decode_raw_start(SubGhz* subghz) {
    FuriString* file_name = furi_string_alloc();
    bool success = false;
    do {
        if(!flipper_format_rewind(subghz_txrx_get_fff_data(subghz->txrx))) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }

        if(!flipper_format_read_string(
               subghz_txrx_get_fff_data(subghz->txrx), "File_name", file_name)) {
            FURI_LOG_E(TAG, "Missing File_name");
            break;
        }

        furi_string_set(subghz->decode_raw_source, file_name);
        success = true;
    } while(false);

    if(success) {

        subghz->decode_raw_file_worker_encoder = subghz_file_encoder_worker_alloc();
        if(subghz_file_encoder_worker_start(
               subghz->decode_raw_file_worker_encoder,
               furi_string_get_cstr(file_name),
               subghz_txrx_radio_device_get_name(subghz->txrx))) {

            furi_delay_ms(100);
        } else {
            success = false;
        }

        if(!success) {
            subghz_file_encoder_worker_free(subghz->decode_raw_file_worker_encoder);
        }
    }

    furi_string_free(file_name);
    return success;
}

bool subghz_scene_decode_raw_next(SubGhz* subghz) {
    LevelDuration level_duration;
    SubGhzReceiver* receiver = subghz_txrx_get_receiver(subghz->txrx);
    for(uint32_t read = SAMPLES_TO_READ_PER_TICK; read > 0; --read) {
        level_duration =
            subghz_file_encoder_worker_get_level_duration(subghz->decode_raw_file_worker_encoder);
        if(!level_duration_is_reset(level_duration)) {
            if(level_duration_is_wait(level_duration)) {
                FURI_LOG_W(TAG, "LD tells wait!");
                return true;
            }
            bool level = level_duration_get_level(level_duration);
            uint32_t duration = level_duration_get_duration(level_duration);
            if(duration > 1000000) {
                FURI_LOG_E(TAG, "LD came with overflow: %ld", duration);
                return true;
            }
            subghz_receiver_decode(receiver, level, duration);
        } else {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneDecodeRAW, SubGhzDecodeRawStateLoaded);
            subghz->state_notifications = SubGhzNotificationStateIDLE;

            if(subghz_history_get_item(subghz->history) == 0) {

                extern void subghz_scene_decode_raw_failed_set_context(bool all_protocols_enabled);
                size_t total = subghz_protocol_registry_count(&subghz_protocol_registry);
                size_t enabled = subghz_protocol_filter_enabled_count(subghz->protocol_filter, total);
                subghz_scene_decode_raw_failed_set_context(enabled == total);
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneDecodeRawFailed);
            } else {
                subghz_view_receiver_add_data_progress(subghz->subghz_receiver, "Done!");
            }
            return false;
        }
    }

    FuriString* progress_str = furi_string_alloc();
    subghz_file_encoder_worker_get_text_progress(
        subghz->decode_raw_file_worker_encoder, progress_str);

    subghz_view_receiver_add_data_progress(
        subghz->subghz_receiver, furi_string_get_cstr(progress_str));

    furi_string_free(progress_str);

    return true;
}

void subghz_scene_decode_raw_on_enter(void* context) {
    SubGhz* subghz = context;
    subghz_decode_plugin_loader_flush();
    subghz_lowram_mitigate(
        SUBGHZ_LOWRAM_SOFTLOCK_THRESHOLD + subghz_txrx_receiver_heap_needed(subghz->txrx));
    subghz_history_release_cache(subghz->history);

    subghz_view_receiver_set_mode(subghz->subghz_receiver, SubGhzViewReceiverModeFile);
    subghz_view_receiver_set_callback(
        subghz->subghz_receiver, subghz_scene_decode_raw_callback, subghz);

    subghz_txrx_set_rx_callback(subghz->txrx, subghz_scene_add_to_history_callback, subghz);

    subghz_txrx_receiver_set_filter(subghz->txrx, SubGhzProtocolFlag_Decodable);
    subghz_txrx_receiver_apply_modulation_filter(subghz->txrx);

    if(scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneDecodeRAW) ==
       SubGhzDecodeRawStateStart) {

        if(memmgr_get_free_heap() < SUBGHZ_DECODE_RAW_MIN_HEAP) {
            /* PC connected: not enough free RAM to decode without OOM -> refuse. */
            subghz->decode_raw_file_worker_encoder = NULL;
            subghz_view_receiver_add_data_progress(
                subghz->subghz_receiver, "RAM too low");
        } else {
            subghz_history_reset(subghz->history);
            if(subghz_scene_decode_raw_start(subghz)) {
                scene_manager_set_scene_state(
                    subghz->scene_manager, SubGhzSceneDecodeRAW, SubGhzDecodeRawStateLoading);
                subghz->state_notifications = SubGhzNotificationStateRx;
            }
        }
    } else {

        subghz_view_receiver_exit(subghz->subghz_receiver);
        for(uint16_t i = 0; i < subghz_history_get_item(subghz->history); i++) {
            subghz_view_receiver_add_item_to_menu(subghz->subghz_receiver);
        }
        subghz_view_receiver_set_idx_menu(subghz->subghz_receiver, subghz->idx_menu_chosen);
    }

    subghz_scene_receiver_update_statusbar(subghz);

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdReceiver);
}

bool subghz_scene_decode_raw_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case SubGhzCustomEventViewReceiverBack:
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneDecodeRAW, SubGhzDecodeRawStateStart);
            subghz->idx_menu_chosen = 0;

            subghz_txrx_set_rx_callback(subghz->txrx, NULL, subghz);

            if(subghz->decode_raw_file_worker_encoder) {
                if(subghz_file_encoder_worker_is_running(
                       subghz->decode_raw_file_worker_encoder)) {
                    subghz_file_encoder_worker_stop(subghz->decode_raw_file_worker_encoder);
                }
                subghz_file_encoder_worker_free(subghz->decode_raw_file_worker_encoder);
                subghz->decode_raw_file_worker_encoder = NULL;
            }

            subghz->state_notifications = SubGhzNotificationStateIDLE;
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneReadRAW, SubGhzCustomEventManagerNoSet);
            scene_manager_search_and_switch_to_previous_scene(
                subghz->scene_manager, SubGhzSceneMoreRAW);
            consumed = true;
            break;
        case SubGhzCustomEventViewReceiverOK:
            subghz->idx_menu_chosen = subghz_view_receiver_get_idx_menu(subghz->subghz_receiver);
            subghz->state_notifications = SubGhzNotificationStateIDLE;
            subghz_txrx_set_rx_callback(subghz->txrx, NULL, subghz);
            if(subghz->decode_raw_file_worker_encoder &&
               subghz_file_encoder_worker_is_running(
                   subghz->decode_raw_file_worker_encoder)) {
                subghz_file_encoder_worker_stop(subghz->decode_raw_file_worker_encoder);
            }
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReceiverInfo);
            consumed = true;
            break;
        case SubGhzCustomEventViewReceiverConfig:
            FURI_LOG_W(TAG, "No config options");
            consumed = true;
            break;
        case SubGhzCustomEventViewReceiverOffDisplay:
            notification_message(subghz->notifications, &sequence_display_backlight_off);
            consumed = true;
            break;
        default:
            break;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        subghz_history_flush(subghz->history);
        switch(subghz->state_notifications) {
        case SubGhzNotificationStateRx:
            notification_message(subghz->notifications, &sequence_blink_cyan_10);
            break;
        case SubGhzNotificationStateRxDone:
            if(subghz_txrx_speaker_get_state(subghz->txrx) == SubGhzSpeakerStateEnable) {
                notification_message(subghz->notifications, &subghz_sequence_rx);
            } else {
                notification_message(subghz->notifications, &subghz_sequence_rx_nosound);
            }
            subghz->state_notifications = SubGhzNotificationStateRx;
            break;
        default:
            break;
        }

        switch(scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneDecodeRAW)) {
        case SubGhzDecodeRawStateLoading:
            subghz_scene_decode_raw_next(subghz);
            break;
        default:
            break;
        }
    }
    return consumed;
}

void subghz_scene_decode_raw_on_exit(void* context) {
    UNUSED(context);
    subghz_lowram_softlock_release();
}
