#include "../subghz_i.h"
#include <lib/subghz/protocols/bin_raw.h>

#include <toolbox/name_generator.h>
#include <string.h>
#include "../helpers/subghz_lowram_guard.h"
#include "../helpers/subghz_decode_plugin_loader.h"

#define TAG "SubGhzSceneReceiver"

/* 1: capture undecoded RF bursts inline as "Unknown RAW" history items so the user
 * sees them without backing out of Read. 0: disable (kill switch). */
#define SUBGHZ_CAPTURE_UNKNOWN_RAW 1
#if SUBGHZ_CAPTURE_UNKNOWN_RAW
static uint32_t s_last_decode_tick = 0; /* tick of last decoder output */
static bool s_burst_signal_seen = false;
#endif

const NotificationSequence subghz_sequence_rx = {
    &message_green_255,

    &message_display_backlight_on,

    &message_vibro_on,
    &message_note_c6,
    &message_delay_50,
    &message_sound_off,
    &message_vibro_off,

    &message_delay_50,
    NULL,
};

const NotificationSequence subghz_sequence_rx_locked = {
    &message_green_255,

    &message_display_backlight_on,

    &message_vibro_on,
    &message_note_c6,
    &message_delay_50,
    &message_sound_off,
    &message_vibro_off,

    &message_delay_500,

    &message_display_backlight_off,
    NULL,
};

const NotificationSequence subghz_sequence_rx_nosound = {
    &message_green_255,

    &message_display_backlight_on,

    &message_vibro_on,
    &message_delay_50,
    &message_vibro_off,

    &message_delay_50,
    NULL,
};

const NotificationSequence subghz_sequence_rx_locked_nosound = {
    &message_green_255,

    &message_display_backlight_on,

    &message_vibro_on,
    &message_delay_50,
    &message_vibro_off,

    &message_delay_500,

    &message_display_backlight_off,
    NULL,
};

static void subghz_scene_receiver_update_statusbar(void* context) {
    SubGhz* subghz = context;
    FuriString* history_stat_str = furi_string_alloc();
    if(!subghz_history_get_text_space_left(subghz->history, history_stat_str)) {
        FuriString* frequency_str = furi_string_alloc();
        FuriString* modulation_str = furi_string_alloc();

#ifdef SUBGHZ_EXT_PRESET_NAME
        if(subghz_history_get_last_index(subghz->history) > 0) {
            subghz_txrx_get_frequency_and_modulation(
                subghz->txrx, frequency_str, modulation_str, false);
        } else {
            subghz_txrx_get_frequency_and_modulation(
                subghz->txrx, frequency_str, modulation_str, true);
        }
#else
        subghz_txrx_get_frequency_and_modulation(
            subghz->txrx, frequency_str, modulation_str, false);
#endif

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
        subghz->state_notifications = SubGhzNotificationStateIDLE;
    }
    furi_string_free(history_stat_str);

    subghz_view_receiver_set_radio_device_type(
        subghz->subghz_receiver, subghz_txrx_radio_device_get(subghz->txrx));
}

#define SUBGHZ_RECEIVER_TUNE_DEBOUNCE_TICKS 3

static uint8_t s_tune_debounce = 0;

static void subghz_scene_receiver_cycle_frequency(SubGhz* subghz, bool next) {
    if(subghz_txrx_hopper_get_state(subghz->txrx) != SubGhzHopperStateOFF) return;
    SubGhzSetting* setting = subghz_txrx_get_setting(subghz->txrx);
    const size_t count = subghz_setting_get_frequency_count(setting);
    if(count == 0) return;
    SubGhzRadioPreset preset = subghz_txrx_get_preset(subghz->txrx);
    size_t index = 0;
    for(size_t i = 0; i < count; i++) {
        if(preset.frequency == subghz_setting_get_frequency(setting, i)) {
            index = i;
            break;
        }
    }
    index = next ? (index + 1) % count : (index + count - 1) % count;
    const uint32_t frequency = subghz_setting_get_frequency(setting, index);
    subghz_txrx_set_preset(
        subghz->txrx,
        furi_string_get_cstr(preset.name),
        frequency,
        preset.data,
        preset.data_size);
    subghz->last_settings->frequency = frequency;
    subghz_setting_set_default_frequency(setting, frequency);
    s_tune_debounce = SUBGHZ_RECEIVER_TUNE_DEBOUNCE_TICKS;
    subghz_scene_receiver_update_statusbar(subghz);
}

static void subghz_scene_receiver_cycle_preset(SubGhz* subghz, bool next) {
    if(subghz_txrx_preset_hopper_get_state(subghz->txrx) != SubGhzPresetHopperStateOFF) return;
    SubGhzSetting* setting = subghz_txrx_get_setting(subghz->txrx);
    const size_t count = subghz_setting_get_preset_count(setting);
    if(count == 0) return;
    SubGhzRadioPreset preset = subghz_txrx_get_preset(subghz->txrx);
    size_t index = 0;
    for(size_t i = 0; i < count; i++) {
        if(!strcmp(subghz_setting_get_preset_name(setting, i), furi_string_get_cstr(preset.name))) {
            index = i;
            break;
        }
    }
    for(size_t step = 0; step < count; step++) {
        index = next ? (index + 1) % count : (index + count - 1) % count;
        if(!subghz->modulation_filter ||
           subghz_modulation_filter_is_enabled(subghz->modulation_filter, index)) {
            break;
        }
    }
    const char* preset_name = subghz_setting_get_preset_name(setting, index);
    uint8_t* preset_data = subghz_setting_get_preset_data(setting, index);
    const size_t preset_data_size = subghz_setting_get_preset_data_size(setting, index);
    subghz_txrx_set_tx_power(preset_data, preset_data_size, subghz->tx_power);
    subghz_txrx_set_preset(
        subghz->txrx, preset_name, preset.frequency, preset_data, preset_data_size);
    subghz->last_settings->preset_index = index;
    s_tune_debounce = SUBGHZ_RECEIVER_TUNE_DEBOUNCE_TICKS;
    subghz_scene_receiver_update_statusbar(subghz);
}

static void subghz_scene_receiver_apply_tune(SubGhz* subghz) {
    s_tune_debounce = 0;
    subghz_txrx_rx_start(subghz->txrx);
    subghz_save_all(subghz);
    subghz_scene_receiver_update_statusbar(subghz);
}

void subghz_scene_receiver_callback(SubGhzCustomEvent event, void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, event);
}

void subghz_scene_receiver_process_auto_save(SubGhz* subghz) {
    uint16_t idx = 0;
    while(subghz_history_find_auto_save_pending(subghz->history, &idx)) {
        FlipperFormat* ff = subghz_history_get_raw_data(subghz->history, idx);
        bool success = false;
        if(ff) {
            FuriString* protocol_name = furi_string_alloc();
            flipper_format_rewind(ff);
            if(!flipper_format_read_string(ff, "Protocol", protocol_name)) {
                furi_string_set(protocol_name, "Unknown");
            }

            char file_name_buf[SUBGHZ_MAX_LEN_NAME] = {0};
            name_generator_make_auto_datetime(
                file_name_buf, SUBGHZ_MAX_LEN_NAME, furi_string_get_cstr(protocol_name), NULL);
            furi_string_free(protocol_name);

            if(subghz->last_settings->file_prefix[0] != '\0') {

                char tmp[SUBGHZ_MAX_LEN_NAME + sizeof(subghz->last_settings->file_prefix)];
                snprintf(
                    tmp, sizeof(tmp), "%s%s", subghz->last_settings->file_prefix, file_name_buf);
                strncpy(file_name_buf, tmp, SUBGHZ_MAX_LEN_NAME - 1);
                file_name_buf[SUBGHZ_MAX_LEN_NAME - 1] = '\0';
            }

            FuriString* file_path = furi_string_alloc();
            furi_string_set(file_path, SUBGHZ_APP_FOLDER);
            furi_string_cat_printf(
                file_path, "/%s%s", file_name_buf, SUBGHZ_APP_FILENAME_EXTENSION);

            success = subghz_save_protocol_to_file(subghz, ff, furi_string_get_cstr(file_path));
            furi_string_free(file_path);
        }

        if(success) {
            FURI_LOG_I(TAG, "Auto-saved history item %u", idx);
            notification_message(subghz->notifications, &sequence_double_vibro);
        } else {
            FURI_LOG_E(TAG, "Auto-save failed for history item %u", idx);
            notification_message(subghz->notifications, &sequence_error);
        }
        subghz_history_set_auto_save_pending(subghz->history, idx, false);
    }
    subghz_history_release_cache(subghz->history);
}

void subghz_scene_receiver_add_decoded(
    SubGhz* subghz,
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    SubGhzRadioPreset* preset,
    SubGhzHistorySource source) {
    if((decoder_base->protocol->flag & subghz->ignore_filter) == 0) {
        SubGhzHistory* history = subghz->history;
        uint16_t idx = subghz_history_get_item(history);

        bool identical_ping = false;
        {
            bool ping_notify = false;
            int16_t ident =
                subghz_history_find_identical(history, decoder_base, preset, &ping_notify);
            if(ident >= 0) {
                identical_ping = true;
                subghz_view_receiver_flash_item(subghz->subghz_receiver, (uint16_t)ident);
                if(ping_notify) {
                    subghz->state_notifications = SubGhzNotificationStateRxDone;
                }
            }
        }
        if(!identical_ping && subghz->last_settings->delete_old_signals) {
            if(subghz_history_get_last_index(subghz->history) >= SUBGHZ_HISTORY_MAX - 1) {
                subghz->state_notifications = SubGhzNotificationStateRx;

                subghz_view_receiver_disable_draw_callback(subghz->subghz_receiver);

                subghz_history_delete_item(subghz->history, 0);
                subghz_view_receiver_delete_item(subghz->subghz_receiver, 0);
                subghz_view_receiver_enable_draw_callback(subghz->subghz_receiver);

                subghz_scene_receiver_update_statusbar(subghz);
                subghz->idx_menu_chosen =
                    subghz_view_receiver_get_idx_menu(subghz->subghz_receiver);
                idx--;
            }
        }
        if(!identical_ping &&
           subghz_history_add_to_history(history, decoder_base, preset, source)) {
            subghz->state_notifications = SubGhzNotificationStateRxDone;

            subghz_view_receiver_add_item_to_menu(subghz->subghz_receiver);

            uint16_t save_idx = idx;
            bool do_auto_save = true;
            int16_t dedup_removed = subghz_history_dedup_last_fiat_hitag2(history);
            if(dedup_removed < 0) {
                dedup_removed = subghz_history_dedup_last_came_holtek(history);
            }
            if(dedup_removed >= 0) {
                subghz_view_receiver_disable_draw_callback(subghz->subghz_receiver);
                subghz_view_receiver_delete_item(
                    subghz->subghz_receiver, (uint16_t)dedup_removed);
                subghz_view_receiver_enable_draw_callback(subghz->subghz_receiver);
                subghz->idx_menu_chosen =
                    subghz_view_receiver_get_idx_menu(subghz->subghz_receiver);
                if((uint16_t)dedup_removed == idx) {
                    do_auto_save = false;
                } else if((uint16_t)dedup_removed < idx) {
                    save_idx = idx - 1;
                }
            }

            subghz_scene_receiver_update_statusbar(subghz);
            if(subghz_history_get_text_space_left(subghz->history, NULL)) {
                notification_message(subghz->notifications, &sequence_error);
            }
            subghz_rx_key_state_set(subghz, SubGhzRxKeyStateAddKey);
            if(subghz->last_settings->auto_save && do_auto_save) {

                subghz_history_set_auto_save_pending(history, save_idx, true);
            }
        }
        if(decoder_base->protocol->name && !strcmp(decoder_base->protocol->name, "Hitag-2")) {
            subghz_receiver_reset_except_by_name(receiver, "Fiat V1");
        } else {
            subghz_receiver_reset(receiver);
        }
    } else {
        FURI_LOG_D(TAG, "%s protocol ignored", decoder_base->protocol->name);
    }
}

static void subghz_scene_add_to_history_callback(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
#if SUBGHZ_CAPTURE_UNKNOWN_RAW
    s_last_decode_tick = furi_get_tick();
#endif
    SubGhzRadioPreset preset = subghz_txrx_get_preset(subghz->txrx);
    subghz_scene_receiver_add_decoded(
        subghz, receiver, decoder_base, &preset, SubGhzHistorySourceNone);
}

void subghz_scene_receiver_notify(SubGhz* subghz) {
    switch(subghz->state_notifications) {
    case SubGhzNotificationStateRx:
        notification_message(subghz->notifications, &sequence_blink_cyan_10);
        break;
    case SubGhzNotificationStateRxDone: {
        bool sound_on = subghz_txrx_speaker_get_state(subghz->txrx) == SubGhzSpeakerStateEnable;
        if(!subghz_is_locked(subghz)) {
            if(sound_on) {
                notification_message(subghz->notifications, &subghz_sequence_rx);
            } else {
                notification_message(subghz->notifications, &subghz_sequence_rx_nosound);
            }
        } else {
            if(sound_on) {
                notification_message(subghz->notifications, &subghz_sequence_rx_locked);
            } else {
                notification_message(subghz->notifications, &subghz_sequence_rx_locked_nosound);
            }
        }
        subghz->state_notifications = SubGhzNotificationStateRx;
        break;
    }
    default:
        break;
    }
}

void subghz_scene_receiver_on_enter(void* context) {
    SubGhz* subghz = context;
    subghz_decode_plugin_loader_flush();
    subghz_lowram_mitigate(
        SUBGHZ_LOWRAM_SOFTLOCK_THRESHOLD + subghz_txrx_receiver_heap_needed(subghz->txrx));
    SubGhzHistory* history = subghz->history;
    subghz_history_release_cache(history);
    subghz_txrx_rx_ring_set_wanted(
        subghz->txrx, SUBGHZ_CAPTURE_UNKNOWN_RAW && subghz->last_settings->capture_unknown_raw);

    if(subghz_rx_key_state_get(subghz) == SubGhzRxKeyStateIDLE) {
        subghz_txrx_set_preset_internal(
            subghz->txrx,
            subghz->last_settings->frequency,
            subghz->last_settings->preset_index,
            subghz->last_settings->tx_power);

        subghz->filter = subghz->last_settings->filter;
        subghz_txrx_receiver_set_filter(subghz->txrx, subghz->filter);
        subghz->ignore_filter = subghz->last_settings->ignore_filter;
        subghz->tx_power = subghz->last_settings->tx_power;

        subghz_history_reset(history);
        subghz_rx_key_state_set(subghz, SubGhzRxKeyStateStart);
        subghz->idx_menu_chosen = 0;
    }

    subghz_view_receiver_set_mode(subghz->subghz_receiver, SubGhzViewReceiverModeLive);
    subghz_view_receiver_set_wave_line(
        subghz->subghz_receiver,
        subghz->last_settings->visualizer_display_mode == SubGhzReadRawVizLine);

    subghz_view_receiver_exit(subghz->subghz_receiver);
    for(uint16_t i = 0; i < subghz_history_get_item(history); i++) {
        subghz_view_receiver_add_item_to_menu(subghz->subghz_receiver);
        subghz_rx_key_state_set(subghz, SubGhzRxKeyStateAddKey);
    }

    subghz_view_receiver_set_callback(
        subghz->subghz_receiver, subghz_scene_receiver_callback, subghz);
    subghz_txrx_set_rx_callback(subghz->txrx, subghz_scene_add_to_history_callback, subghz);

    if(!subghz_history_get_text_space_left(subghz->history, NULL)) {
        subghz->state_notifications = SubGhzNotificationStateRx;
    }

    if(subghz->last_settings->enable_hopping) {
        subghz_txrx_hopper_set_state(subghz->txrx, SubGhzHopperStateRunning);
    } else {
        subghz_txrx_hopper_set_state(subghz->txrx, SubGhzHopperStateOFF);
    }

    if(subghz->last_settings->enable_preset_hopping) {
        subghz_txrx_preset_hopper_set_state(subghz->txrx, SubGhzPresetHopperStateRunning);
    } else {
        subghz_txrx_preset_hopper_set_state(subghz->txrx, SubGhzPresetHopperStateOFF);
    }

    subghz_txrx_rx_start(subghz->txrx);
    subghz_view_receiver_set_idx_menu(subghz->subghz_receiver, subghz->idx_menu_chosen);

    furi_check(
        subghz_txrx_load_decoder_by_name_protocol(subghz->txrx, SUBGHZ_PROTOCOL_BIN_RAW_NAME));

    subghz_scene_receiver_update_statusbar(subghz);

    subghz_view_receiver_set_lock(subghz->subghz_receiver, subghz_is_locked(subghz));

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdReceiver);
}

bool subghz_scene_receiver_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom) {

        subghz->idx_menu_chosen = subghz_view_receiver_get_idx_menu(subghz->subghz_receiver);

        switch(event.event) {
        case SubGhzCustomEventViewReceiverBack:

            subghz->state_notifications = SubGhzNotificationStateIDLE;
            subghz_txrx_stop(subghz->txrx);
            subghz_txrx_hopper_set_state(subghz->txrx, SubGhzHopperStateOFF);
            subghz_txrx_preset_hopper_set_state(subghz->txrx, SubGhzPresetHopperStateOFF);
            subghz_txrx_set_rx_callback(subghz->txrx, NULL, subghz);

            if(subghz_rx_key_state_get(subghz) == SubGhzRxKeyStateAddKey) {
                subghz_rx_key_state_set(subghz, SubGhzRxKeyStateExit);
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneNeedSaving);
            } else {
                subghz_rx_key_state_set(subghz, SubGhzRxKeyStateIDLE);
                subghz_txrx_set_default_preset(subghz->txrx, subghz->last_settings->frequency);
                scene_manager_search_and_switch_to_previous_scene(
                    subghz->scene_manager, SubGhzSceneStart);
            }
            consumed = true;
            break;
        case SubGhzCustomEventViewReceiverOK: {
#if SUBGHZ_CAPTURE_UNKNOWN_RAW
            /* Captured RAW items serialize Protocol
             * "RAW"; route them to the Unknown-RAW menu. Everything else -> info. */
            const char* ok_proto =
                subghz_history_get_protocol_name(subghz->history, subghz->idx_menu_chosen);
            bool is_raw_item = (ok_proto != NULL) && (strcmp(ok_proto, "RAW") == 0);
            scene_manager_next_scene(
                subghz->scene_manager,
                is_raw_item ? SubGhzSceneUnknownRaw : SubGhzSceneReceiverInfo);
#else
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReceiverInfo);
#endif
            consumed = true;
            break;
        }
        case SubGhzCustomEventViewReceiverDeleteItem:
            subghz->state_notifications = SubGhzNotificationStateRx;

            subghz_view_receiver_disable_draw_callback(subghz->subghz_receiver);

            subghz_history_delete_item(subghz->history, subghz->idx_menu_chosen);
            subghz_view_receiver_delete_item(
                subghz->subghz_receiver,
                subghz_view_receiver_get_idx_menu(subghz->subghz_receiver));
            subghz_view_receiver_enable_draw_callback(subghz->subghz_receiver);

            subghz_scene_receiver_update_statusbar(subghz);
            subghz->idx_menu_chosen = subghz_view_receiver_get_idx_menu(subghz->subghz_receiver);
            if(subghz_history_get_last_index(subghz->history) == 0) {
                subghz_rx_key_state_set(subghz, SubGhzRxKeyStateStart);
            }
            consumed = true;
            break;
        case SubGhzCustomEventViewReceiverConfig:

            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzViewIdReceiver, SubGhzCustomEventManagerSet);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReceiverConfig);
            consumed = true;
            break;
        case SubGhzCustomEventViewReceiverOffDisplay:
            notification_message(subghz->notifications, &sequence_display_backlight_off);
            consumed = true;
            break;
        case SubGhzCustomEventViewReceiverUnlock:
            subghz_unlock(subghz);
            consumed = true;
            break;
        case SubGhzCustomEventViewReceiverNextFrequency:
            subghz_scene_receiver_cycle_frequency(subghz, true);
            consumed = true;
            break;
        case SubGhzCustomEventViewReceiverPrevFrequency:
            subghz_scene_receiver_cycle_frequency(subghz, false);
            consumed = true;
            break;
        case SubGhzCustomEventViewReceiverNextPreset:
            subghz_scene_receiver_cycle_preset(subghz, true);
            consumed = true;
            break;
        case SubGhzCustomEventViewReceiverPrevPreset:
            subghz_scene_receiver_cycle_preset(subghz, false);
            consumed = true;
            break;
        default:
            break;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        if(s_tune_debounce > 0 && --s_tune_debounce == 0) {
            subghz_scene_receiver_apply_tune(subghz);
        }
#if SUBGHZ_CAPTURE_UNKNOWN_RAW
        /* A real burst ended (ring latched+frozen). If no decoder fired for it,
         * store it inline as an Unknown RAW item, then reset (unfreeze) the ring. */
        if(subghz_txrx_burst_ready(subghz->txrx)) {
            /* Runtime toggle (Read config). OFF -> latch is still consumed+reset below
             * so the ring never stays frozen, but nothing is added: only the built-in 65. */
            if(subghz->last_settings->capture_unknown_raw && s_burst_signal_seen &&
               (furi_get_tick() - s_last_decode_tick) >= 400) {
                FlipperFormat* ff = subghz_txrx_rx_ring_build_raw_ff(subghz->txrx);
                if(ff) {
                    SubGhzRadioPreset preset = subghz_txrx_get_preset(subghz->txrx);
                    int16_t idx =
                        subghz_history_add_raw(subghz->history, ff, &preset, "Unknown RAW");
                    if(idx < 0) {
                        flipper_format_free(ff); /* history didn't take it */
                    } else {
                        subghz_view_receiver_add_item_to_menu(subghz->subghz_receiver);
                        subghz_rx_key_state_set(subghz, SubGhzRxKeyStateAddKey);
                        subghz_scene_receiver_update_statusbar(subghz);
                    }
                }
            }
            subghz_txrx_rx_ring_reset(subghz->txrx); /* clear latch + unfreeze */
            s_burst_signal_seen = false;
        }
#endif
        if(subghz->last_settings->auto_save) {
            subghz_scene_receiver_process_auto_save(subghz);
        }
        subghz_history_flush(subghz->history);
        if(subghz_txrx_hopper_get_state(subghz->txrx) != SubGhzHopperStateOFF) {
            subghz_txrx_hopper_update(subghz->txrx, subghz->last_settings->hopping_threshold);
            subghz_scene_receiver_update_statusbar(subghz);
        }
        if(subghz_txrx_preset_hopper_get_state(subghz->txrx) != SubGhzPresetHopperStateOFF) {
            subghz_txrx_preset_hopper_update(subghz->txrx, subghz->last_settings->preset_hopping_threshold);
            subghz_scene_receiver_update_statusbar(subghz);
        }

        SubGhzThresholdRssiData ret_rssi = subghz_threshold_get_rssi_data(
            subghz->threshold_rssi, subghz_txrx_radio_device_get_rssi(subghz->txrx));

        subghz_receiver_rssi(subghz->subghz_receiver, ret_rssi.rssi);
#if SUBGHZ_CAPTURE_UNKNOWN_RAW
        if(ret_rssi.is_above) s_burst_signal_seen = true;
#endif
        if(READ_BIT(subghz->filter, SubGhzProtocolFlag_BinRAW) > 0) {
            subghz_protocol_decoder_bin_raw_data_input_rssi(
                (SubGhzProtocolDecoderBinRAW*)subghz_txrx_get_decoder(subghz->txrx), ret_rssi.rssi);
        }

        subghz_scene_receiver_notify(subghz);
    }
    return consumed;
}

void subghz_scene_receiver_on_exit(void* context) {
    SubGhz* subghz = context;
    if(s_tune_debounce > 0) {
        s_tune_debounce = 0;
        subghz_save_all(subghz);
    }
    subghz_lowram_softlock_release();
}
