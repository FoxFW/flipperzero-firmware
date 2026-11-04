#include "../subghz_i.h"
#include "../helpers/subghz_lowram_guard.h"
#include "../helpers/subghz_decode_plugin_loader.h"

#define TAG "SubGhzSceneReceiverDual"

static SubGhzRadioDeviceType s_dual_restore_device = SubGhzRadioDeviceTypeInternal;

static void subghz_scene_receiver_dual_view_callback(SubGhzCustomEvent event, void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, event);
}

static void subghz_scene_receiver_dual_rx_callback(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    const bool ext = subghz_txrx_dual_is_ext_receiver(subghz->txrx, receiver);
    SubGhzRadioPreset preset = ext ? subghz_txrx_dual_get_ext_preset(subghz->txrx) :
                                     subghz_txrx_get_preset(subghz->txrx);
    subghz_scene_receiver_add_decoded(
        subghz,
        receiver,
        decoder_base,
        &preset,
        ext ? SubGhzHistorySourceExternal : SubGhzHistorySourceInternal);
}

static uint32_t subghz_scene_receiver_dual_valid_preset(SubGhzSetting* setting, uint32_t index, uint32_t fallback) {
    const size_t count = subghz_setting_get_preset_count(setting);
    if(index < count) return index;
    if(fallback < count) return fallback;
    return 0;
}

static void subghz_scene_receiver_dual_apply_presets(SubGhz* subghz) {
    SubGhzSetting* setting = subghz_txrx_get_setting(subghz->txrx);
    SubGhzLastSettings* last = subghz->last_settings;
    last->dual_int_preset_index = subghz_scene_receiver_dual_valid_preset(
        setting, last->dual_int_preset_index, SUBGHZ_LAST_SETTING_DEFAULT_DUAL_INT_PRESET);
    last->dual_ext_preset_index = subghz_scene_receiver_dual_valid_preset(
        setting, last->dual_ext_preset_index, SUBGHZ_LAST_SETTING_DEFAULT_DUAL_EXT_PRESET);

    subghz_txrx_set_preset(
        subghz->txrx,
        subghz_setting_get_preset_name(setting, last->dual_int_preset_index),
        last->dual_int_frequency,
        subghz_setting_get_preset_data(setting, last->dual_int_preset_index),
        subghz_setting_get_preset_data_size(setting, last->dual_int_preset_index));
    subghz_txrx_dual_set_ext_preset(
        subghz->txrx,
        subghz_setting_get_preset_name(setting, last->dual_ext_preset_index),
        last->dual_ext_frequency,
        subghz_setting_get_preset_data(setting, last->dual_ext_preset_index),
        subghz_setting_get_preset_data_size(setting, last->dual_ext_preset_index));
}

static void subghz_scene_receiver_dual_update_statusbar(SubGhz* subghz) {
    FuriString* stat = furi_string_alloc();
    if(subghz_history_get_text_space_left(subghz->history, stat)) {
        furi_string_set(stat, "FULL");
    }
    subghz_view_receiver_add_data_statusbar(
        subghz->subghz_receiver, "", "", furi_string_get_cstr(stat), false, false);

    SubGhzRadioPreset int_preset = subghz_txrx_get_preset(subghz->txrx);
    SubGhzRadioPreset ext_preset = subghz_txrx_dual_get_ext_preset(subghz->txrx);
    FuriString* int_str = furi_string_alloc_printf(
        "%lu.%02lu %s",
        int_preset.frequency / 1000000 % 1000,
        int_preset.frequency / 10000 % 100,
        furi_string_get_cstr(int_preset.name));
    FuriString* ext_str = furi_string_alloc_printf(
        "%lu.%02lu %s",
        ext_preset.frequency / 1000000 % 1000,
        ext_preset.frequency / 10000 % 100,
        furi_string_get_cstr(ext_preset.name));
    subghz_view_receiver_set_dual_info(
        subghz->subghz_receiver, furi_string_get_cstr(int_str), furi_string_get_cstr(ext_str));
    furi_string_free(int_str);
    furi_string_free(ext_str);
    furi_string_free(stat);
}

static void subghz_scene_receiver_dual_stop(SubGhz* subghz, SubGhzRadioDeviceType main_device) {
    subghz_txrx_dual_rx_stop(subghz->txrx, main_device);
}

void subghz_scene_receiver_dual_on_enter(void* context) {
    SubGhz* subghz = context;
    SubGhzHistory* history = subghz->history;
    subghz_decode_plugin_loader_flush();

    if(subghz_rx_key_state_get(subghz) == SubGhzRxKeyStateIDLE) {
        s_dual_restore_device = subghz_txrx_radio_device_get(subghz->txrx);
        subghz->filter = SubGhzProtocolFlag_Decodable;
        subghz_txrx_receiver_set_filter(subghz->txrx, subghz->filter);
        subghz->ignore_filter = subghz->last_settings->ignore_filter;
        subghz_history_reset(history);
        subghz_rx_key_state_set(subghz, SubGhzRxKeyStateStart);
        subghz->idx_menu_chosen = 0;
    }

    subghz_scene_receiver_dual_apply_presets(subghz);
    subghz_txrx_hopper_set_state(subghz->txrx, SubGhzHopperStateOFF);
    subghz_txrx_preset_hopper_set_state(subghz->txrx, SubGhzPresetHopperStateOFF);
    subghz_txrx_rx_ring_set_wanted(subghz->txrx, false);
    subghz_lowram_mitigate(
        SUBGHZ_LOWRAM_SOFTLOCK_THRESHOLD + subghz_txrx_dual_heap_needed(subghz->txrx));
    subghz_history_release_cache(history);

    subghz_view_receiver_set_mode(subghz->subghz_receiver, SubGhzViewReceiverModeLive);
    subghz_view_receiver_set_wave_line(
        subghz->subghz_receiver,
        subghz->last_settings->visualizer_display_mode == SubGhzReadRawVizLine);
    subghz_view_receiver_exit(subghz->subghz_receiver);
    subghz_view_receiver_set_dual(subghz->subghz_receiver, true);
    for(uint16_t i = 0; i < subghz_history_get_item(history); i++) {
        subghz_view_receiver_add_item_to_menu(subghz->subghz_receiver);
        subghz_rx_key_state_set(subghz, SubGhzRxKeyStateAddKey);
    }
    subghz_view_receiver_set_dual_dashboard(
        subghz->subghz_receiver, subghz_history_get_item(history) == 0);

    subghz_view_receiver_set_callback(
        subghz->subghz_receiver, subghz_scene_receiver_dual_view_callback, subghz);
    subghz_txrx_set_rx_callback(subghz->txrx, subghz_scene_receiver_dual_rx_callback, subghz);

    if(!subghz_txrx_dual_rx_start(subghz->txrx)) {
        FURI_LOG_E(TAG, "External module not available");
        subghz_txrx_set_rx_callback(subghz->txrx, NULL, subghz);
        subghz_rx_key_state_set(subghz, SubGhzRxKeyStateIDLE);
        subghz_dialog_message_no_ext_module(subghz);
        scene_manager_search_and_switch_to_previous_scene(subghz->scene_manager, SubGhzSceneStart);
        return;
    }

    if(!subghz_history_get_text_space_left(history, NULL)) {
        subghz->state_notifications = SubGhzNotificationStateRx;
    }

    subghz_view_receiver_set_idx_menu(subghz->subghz_receiver, subghz->idx_menu_chosen);
    subghz_scene_receiver_dual_update_statusbar(subghz);
    subghz_view_receiver_set_lock(subghz->subghz_receiver, subghz_is_locked(subghz));
    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdReceiver);
}

bool subghz_scene_receiver_dual_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom) {
        subghz->idx_menu_chosen = subghz_view_receiver_get_idx_menu(subghz->subghz_receiver);

        switch(event.event) {
        case SubGhzCustomEventViewReceiverBack:
            subghz->state_notifications = SubGhzNotificationStateIDLE;
            subghz_scene_receiver_dual_stop(subghz, s_dual_restore_device);
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
            const SubGhzHistorySource source =
                subghz_history_get_source(subghz->history, subghz->idx_menu_chosen);
            subghz->state_notifications = SubGhzNotificationStateIDLE;
            subghz_scene_receiver_dual_stop(
                subghz,
                source == SubGhzHistorySourceExternal ? SubGhzRadioDeviceTypeExternalCC1101 :
                                                        SubGhzRadioDeviceTypeInternal);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReceiverInfo);
            consumed = true;
            break;
        }
        case SubGhzCustomEventViewReceiverDeleteItem:
            subghz->state_notifications = SubGhzNotificationStateRx;
            subghz_txrx_dual_lock(subghz->txrx);
            subghz_view_receiver_disable_draw_callback(subghz->subghz_receiver);
            subghz_history_delete_item(subghz->history, subghz->idx_menu_chosen);
            subghz_view_receiver_delete_item(
                subghz->subghz_receiver,
                subghz_view_receiver_get_idx_menu(subghz->subghz_receiver));
            subghz_view_receiver_enable_draw_callback(subghz->subghz_receiver);
            subghz_txrx_dual_unlock(subghz->txrx);
            subghz_scene_receiver_dual_update_statusbar(subghz);
            subghz->idx_menu_chosen = subghz_view_receiver_get_idx_menu(subghz->subghz_receiver);
            if(subghz_history_get_last_index(subghz->history) == 0) {
                subghz_rx_key_state_set(subghz, SubGhzRxKeyStateStart);
            }
            consumed = true;
            break;
        case SubGhzCustomEventViewReceiverConfig:
            subghz->state_notifications = SubGhzNotificationStateIDLE;
            subghz_scene_receiver_dual_stop(subghz, SubGhzRadioDeviceTypeInternal);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReceiverDualConfig);
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
        default:
            break;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        if(subghz->last_settings->auto_save) {
            subghz_scene_receiver_process_auto_save(subghz);
        }
        subghz_history_flush(subghz->history);
        subghz_receiver_rssi(
            subghz->subghz_receiver, subghz_txrx_radio_device_get_rssi(subghz->txrx));
        subghz_receiver_rssi_ext(
            subghz->subghz_receiver, subghz_txrx_dual_get_rssi_ext(subghz->txrx));
        subghz_scene_receiver_dual_update_statusbar(subghz);
        subghz_scene_receiver_notify(subghz);
    }
    return consumed;
}

void subghz_scene_receiver_dual_on_exit(void* context) {
    UNUSED(context);
    subghz_lowram_softlock_release();
}
