#include "subghz_config_scene_loader.h"
#include "subghz_config_scene.h"
#include "../subghz_i.h"
#include "../subghz_protocol_filter.h"
#include <lib/subghz/subghz_protocol_registry.h>
#include <applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.h>
#include <string.h>
#include <targets/f7/furi_hal/furi_hal_subghz.h>

#define SUBGHZ_RAW_EDIT_FAP_PATH EXT_PATH("apps/Sub-GHz/subghz_raw_edit.fap")
void subghz_blank_transition_draw_cb(Canvas* canvas, void* ctx);

#include <furi.h>
#include <flipper_application/plugins/plugin_manager.h>
#include <loader/firmware_api/firmware_api.h>
#include <loader/loader.h>

#define TAG "SubGhzConfigLoader"
#define SUBGHZ_CONFIG_MIN_FREE_HEAP 8192u

typedef struct {
    PluginManager* manager;
    const SubGhzConfigScenePlugin* plugin;
    SubGhzConfigHostApi api;
    uint32_t scene_id;
    uint8_t pending_nav;
    bool pending_prev;
    uint8_t pending_nav2_kind;
    uint32_t pending_nav2_target;
    bool nav_child;
    char fal[40];
} SubGhzConfigActive;

static SubGhzConfigActive s_active = {0};

static SubGhzSetting* host_get_setting(void* ctx) {
    return subghz_txrx_get_setting(((SubGhz*)ctx)->txrx);
}
static VariableItemList* host_get_item_list(void* ctx) {
    return ((SubGhz*)ctx)->variable_item_list;
}
static SubGhzLastSettings* host_get_last_settings(void* ctx) {
    return ((SubGhz*)ctx)->last_settings;
}
static void host_switch_to_item_list_view(void* ctx) {
    SubGhz* subghz = ctx;
    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdVariableItemList);
}
static void host_save_all(void* ctx) {
    subghz_save_all((SubGhz*)ctx);
}
static ViewDispatcher* host_get_view_dispatcher(void* ctx) {
    return ((SubGhz*)ctx)->view_dispatcher;
}
static void host_set_scene_state(void* ctx, uint32_t state) {
    SubGhz* subghz = ctx;
    scene_manager_set_scene_state(subghz->scene_manager, s_active.scene_id, state);
}
static void host_open_scene(void* ctx, SubGhzConfigNavTarget target) {

    UNUSED(ctx);
    s_active.pending_nav = (uint8_t)target + 1u;
}
static bool host_mod_filter_is_enabled(void* ctx, size_t index) {
    SubGhz* subghz = ctx;
    if(!subghz->modulation_filter) return false;
    return subghz_modulation_filter_is_enabled(subghz->modulation_filter, index);
}
static void host_mod_filter_set_enabled(void* ctx, size_t index, bool enabled) {
    SubGhz* subghz = ctx;
    if(subghz->modulation_filter)
        subghz_modulation_filter_set_enabled(subghz->modulation_filter, index, enabled);
}
static void host_set_freq_offset_hw(void* ctx, int32_t offset_hz) {
    subghz_txrx_set_frequency_offset(((SubGhz*)ctx)->txrx, offset_hz);
}
static bool host_device_external_available(void* ctx) {
    return subghz_txrx_radio_device_is_external_connected(
        ((SubGhz*)ctx)->txrx, SUBGHZ_DEVICE_CC1101_EXT_NAME);
}
static uint8_t host_device_get_index(void* ctx) {
    return (subghz_txrx_radio_device_get(((SubGhz*)ctx)->txrx) ==
            SubGhzRadioDeviceTypeExternalCC1101) ?
               1 :
               0;
}
static uint8_t host_device_set_index(void* ctx, uint8_t index) {
    SubGhz* subghz = ctx;
    uint8_t applied = index;
    if(index == 1 &&
       !subghz_txrx_radio_device_is_external_connected(subghz->txrx, SUBGHZ_DEVICE_CC1101_EXT_NAME)) {
        applied = 0;
    }
    subghz_txrx_radio_device_set(
        subghz->txrx,
        applied == 1 ? SubGhzRadioDeviceTypeExternalCC1101 : SubGhzRadioDeviceTypeInternal);
    return applied;
}
static uint8_t host_get_tx_power(void* ctx) {
    return ((SubGhz*)ctx)->tx_power;
}
static void host_set_tx_power(void* ctx, uint8_t index) {
    SubGhz* subghz = ctx;
    subghz->tx_power = index;
    subghz->last_settings->tx_power = index;
}
static void host_set_debug_pin_state(void* ctx, bool on) {
    subghz_txrx_set_debug_pin_state(((SubGhz*)ctx)->txrx, on);
}
static bool host_get_debug_pin_state(void* ctx) {
    return subghz_txrx_get_debug_pin_state(((SubGhz*)ctx)->txrx);
}
static void host_threshold_rssi_set(void* ctx, float value) {
    subghz_threshold_rssi_set(((SubGhz*)ctx)->threshold_rssi, value);
}
static float host_threshold_rssi_get(void* ctx) {
    return subghz_threshold_rssi_get(((SubGhz*)ctx)->threshold_rssi);
}
static void host_reseat_radio_device(void* ctx) {
    SubGhz* subghz = ctx;
    const SubGhzRadioDeviceType current = subghz_txrx_radio_device_get(subghz->txrx);
    if(current != SubGhzRadioDeviceTypeInternal) {
        subghz_txrx_radio_device_set(subghz->txrx, SubGhzRadioDeviceTypeInternal);
        subghz_txrx_radio_device_set(subghz->txrx, current);
    }
}
static void host_set_ext_leds_and_amp(void* ctx, bool on) {
    UNUSED(ctx);
    furi_hal_subghz_set_ext_leds_and_amp(on);
}
static void host_set_dangerous_frequency(void* ctx, bool on) {
    UNUSED(ctx);
    furi_hal_subghz_set_dangerous_frequency(on);
}
static void host_set_rolling_counter_mult(void* ctx, int32_t mult) {
    UNUSED(ctx);
    furi_hal_subghz_set_rolling_counter_mult(mult);
}
static int32_t host_get_rolling_counter_mult(void* ctx) {
    UNUSED(ctx);
    return furi_hal_subghz_get_rolling_counter_mult();
}
static bool host_is_debug_mode(void* ctx) {
    UNUSED(ctx);
    return furi_hal_rtc_is_flag_set(FuriHalRtcFlagDebug);
}

#define RC_LAST_SETTINGS_FILE EXT_PATH("subghz/assets/last_subghz.settings")
#define RC_FA_PRESET_IDX_KEY  "FAPresetIndex"

static void host_fa_sync(int preset_index) {
    if(preset_index < 0) return;
    Storage* s = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(s);
    if(flipper_format_file_open_existing(ff, RC_LAST_SETTINGS_FILE)) {
        uint32_t pi = (uint32_t)preset_index;
        if(!flipper_format_update_uint32(ff, RC_FA_PRESET_IDX_KEY, &pi, 1)) {
            flipper_format_rewind(ff);
            flipper_format_write_uint32(ff, RC_FA_PRESET_IDX_KEY, &pi, 1);
        }
    }
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
}

static bool host_is_read_raw_mode(void* ctx) {
    SubGhz* subghz = ctx;
    return scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneReadRAW) ==
           SubGhzCustomEventManagerSet;
}
static void host_clear_read_raw_state(void* ctx) {
    SubGhz* subghz = ctx;
    scene_manager_set_scene_state(
        subghz->scene_manager, SubGhzSceneReadRAW, SubGhzCustomEventManagerNoSet);
}
static void host_previous_scene(void* ctx) {

    UNUSED(ctx);
    s_active.pending_prev = true;
}
static void host_lock_keyboard(void* ctx) {
    subghz_lock((SubGhz*)ctx);
}
static bool host_hopper_get_running(void* ctx) {
    return subghz_txrx_hopper_get_state(((SubGhz*)ctx)->txrx) != SubGhzHopperStateOFF;
}
static void host_hopper_set_running(void* ctx, bool running) {
    subghz_txrx_hopper_set_state(
        ((SubGhz*)ctx)->txrx, running ? SubGhzHopperStateRunning : SubGhzHopperStateOFF);
}
static bool host_preset_hopper_get_running(void* ctx) {
    return subghz_txrx_preset_hopper_get_state(((SubGhz*)ctx)->txrx) != SubGhzPresetHopperStateOFF;
}
static void host_preset_hopper_set_running(void* ctx, bool running) {
    subghz_txrx_preset_hopper_set_state(
        ((SubGhz*)ctx)->txrx,
        running ? SubGhzPresetHopperStateRunning : SubGhzPresetHopperStateOFF);
}
static void host_preset_hopper_pause(void* ctx) {
    subghz_txrx_preset_hopper_pause(((SubGhz*)ctx)->txrx);
}
static bool host_speaker_get_enabled(void* ctx) {
    return subghz_txrx_speaker_get_state(((SubGhz*)ctx)->txrx) != SubGhzSpeakerStateShutdown;
}
static void host_speaker_set_enabled(void* ctx, bool enabled) {
    subghz_txrx_speaker_set_state(
        ((SubGhz*)ctx)->txrx, enabled ? SubGhzSpeakerStateEnable : SubGhzSpeakerStateShutdown);
}
static uint32_t host_get_ignore_filter(void* ctx) {
    return ((SubGhz*)ctx)->ignore_filter;
}
static void host_set_ignore_filter(void* ctx, uint32_t value) {
    SubGhz* subghz = ctx;
    subghz->ignore_filter = value;
    subghz->last_settings->ignore_filter = value;
}
static uint32_t host_get_filter(void* ctx) {
    return ((SubGhz*)ctx)->filter;
}
static void host_set_filter(void* ctx, uint32_t value) {
    SubGhz* subghz = ctx;
    subghz->filter = value;
    subghz_txrx_receiver_set_filter(subghz->txrx, value);
    subghz->last_settings->filter = value;
}
static uint32_t host_get_preset_frequency(void* ctx) {
    return subghz_txrx_get_preset(((SubGhz*)ctx)->txrx).frequency;
}
static uint8_t host_current_preset_index(void* ctx) {
    SubGhz* subghz = ctx;
    SubGhzSetting* setting = subghz_txrx_get_setting(subghz->txrx);
    SubGhzRadioPreset preset = subghz_txrx_get_preset(subghz->txrx);
    const char* name = furi_string_get_cstr(preset.name);
    for(size_t i = 0; i < subghz_setting_get_preset_count(setting); i++) {
        if(!strcmp(subghz_setting_get_preset_name(setting, i), name)) return (uint8_t)i;
    }
    return 0;
}
static void host_current_preset_name(void* ctx, char* out, size_t size) {
    SubGhzRadioPreset preset = subghz_txrx_get_preset(((SubGhz*)ctx)->txrx);
    snprintf(out, size, "%s", furi_string_get_cstr(preset.name));
}
static uint16_t host_predefined_count(void* ctx) {
    SubGhz* subghz = ctx;
    subghz->predefined_count = subghz_predefined_get_count();
    return subghz->predefined_count;
}
static void host_predefined_get_name(void* ctx, uint16_t index, char* out, size_t size) {
    SubGhz* subghz = ctx;
    SubGhzSetting* setting = subghz_txrx_get_setting(subghz->txrx);
    SubGhzPredefined pd = {0};
    subghz_predefined_get_by_index(&pd, index, subghz->predefined_count, setting);
    snprintf(out, size, "%s", pd.name ? pd.name : "None");
    subghz_predefined_free(&pd);
}
static void host_apply_frequency(void* ctx, uint32_t frequency) {
    SubGhz* subghz = ctx;
    SubGhzSetting* setting = subghz_txrx_get_setting(subghz->txrx);
    SubGhzRadioPreset preset = subghz_txrx_get_preset(subghz->txrx);
    subghz_txrx_set_tx_power(preset.data, preset.data_size, subghz->tx_power);
    subghz_txrx_set_preset(
        subghz->txrx, furi_string_get_cstr(preset.name), frequency, preset.data, preset.data_size);
    subghz->last_settings->frequency = frequency;
    subghz_setting_set_default_frequency(setting, frequency);
}
static void host_apply_preset_at_frequency(void* ctx, uint32_t frequency) {

    SubGhz* subghz = ctx;
    SubGhzRadioPreset preset = subghz_txrx_get_preset(subghz->txrx);
    subghz_txrx_set_tx_power(preset.data, preset.data_size, subghz->tx_power);
    subghz_txrx_set_preset(
        subghz->txrx, furi_string_get_cstr(preset.name), frequency, preset.data, preset.data_size);
}
static void host_apply_preset(void* ctx, uint8_t index) {
    SubGhz* subghz = ctx;
    SubGhzSetting* setting = subghz_txrx_get_setting(subghz->txrx);
    const char* preset_name = subghz_setting_get_preset_name(setting, index);
    SubGhzRadioPreset preset = subghz_txrx_get_preset(subghz->txrx);
    uint8_t* preset_data = subghz_setting_get_preset_data(setting, index);
    size_t preset_data_size = subghz_setting_get_preset_data_size(setting, index);
    subghz_txrx_set_tx_power(preset_data, preset_data_size, subghz->tx_power);
    subghz_txrx_set_preset(
        subghz->txrx, preset_name, preset.frequency, preset_data, preset_data_size);
    subghz->last_settings->preset_index = index;
}
static bool host_apply_predefined(void* ctx, uint16_t index, char* name_out, size_t name_size) {
    SubGhz* subghz = ctx;
    SubGhzSetting* setting = subghz_txrx_get_setting(subghz->txrx);
    SubGhzPredefined pd = {0};
    subghz_predefined_get_by_index(&pd, index, subghz->predefined_count, setting);
    if(name_out && name_size) snprintf(name_out, name_size, "%s", pd.name ? pd.name : "None");
    bool active = (pd.index != 0) && (pd.preset != NULL);
    if(active) {
        SubGhzRadioPreset* p = pd.preset;
        subghz_txrx_set_tx_power(p->data, p->data_size, subghz->tx_power);
        subghz_txrx_set_preset(
            subghz->txrx, furi_string_get_cstr(p->name), p->frequency, p->data, p->data_size);
        subghz->last_settings->frequency = p->frequency;
        subghz_setting_set_default_frequency(setting, p->frequency);
        int pidx = subghz_setting_get_inx_preset_by_name(setting, furi_string_get_cstr(p->name));
        if(pidx >= 0) subghz->last_settings->preset_index = pidx;
    }
    subghz->last_settings->predefined_index = pd.index;
    subghz_predefined_free(&pd);
    return active;
}
static void host_reset_to_default(void* ctx) {
    SubGhz* subghz = ctx;
    subghz_txrx_set_preset_internal(
        subghz->txrx,
        SUBGHZ_LAST_SETTING_DEFAULT_FREQUENCY,
        SUBGHZ_LAST_SETTING_DEFAULT_PRESET,
        subghz->tx_power);
    SubGhzSetting* setting = subghz_txrx_get_setting(subghz->txrx);
    SubGhzRadioPreset preset = subghz_txrx_get_preset(subghz->txrx);
    int preset_index =
        subghz_setting_get_inx_preset_by_name(setting, furi_string_get_cstr(preset.name));
    subghz->last_settings->frequency = SUBGHZ_LAST_SETTING_DEFAULT_FREQUENCY;
    subghz->last_settings->preset_index = preset_index;
    subghz_threshold_rssi_set(subghz->threshold_rssi, SUBGHZ_RAW_THRESHOLD_DEFAULT);
    subghz->last_settings->rssi = SUBGHZ_RAW_THRESHOLD_DEFAULT;
    subghz->filter = SubGhzProtocolFlag_Decodable;
    subghz->ignore_filter = 0x00;
    subghz_txrx_receiver_set_filter(subghz->txrx, subghz->filter);
    subghz->last_settings->ignore_filter = subghz->ignore_filter;
    subghz->last_settings->filter = subghz->filter;
    subghz->last_settings->delete_old_signals = false;
    subghz->last_settings->auto_save = false;
    subghz->last_settings->capture_unknown_raw = false;
    subghz->last_settings->show_psa_encrypted = false;
    subghz->last_settings->tx_power = subghz->tx_power = 0;
    subghz_txrx_speaker_set_state(subghz->txrx, SubGhzSpeakerStateShutdown);
    subghz_txrx_hopper_set_state(subghz->txrx, SubGhzHopperStateOFF);
    subghz->last_settings->enable_hopping = false;
    subghz->last_settings->enable_preset_hopping = false;
    subghz->last_settings->preset_hopping_threshold =
        SUBGHZ_LAST_SETTING_DEFAULT_PRESET_HOPPING_THRESHOLD;
    subghz_txrx_preset_hopper_set_state(subghz->txrx, SubGhzPresetHopperStateOFF);
    subghz_save_all(subghz);
    host_fa_sync(subghz->last_settings->preset_index);
}
static void host_sync_preset_to_fa(void* ctx, int preset_index) {
    UNUSED(ctx);
    host_fa_sync(preset_index);
}

static const NotificationSequence subghz_config_seq_sd_error = {
    &message_red_255,
    &message_green_255,
    &message_do_not_reset,
    NULL,
};

static uint32_t subghz_config_map_scene(SubGhzConfigSceneId id) {
    switch(id) {
    case SubGhzConfigSceneStart:
        return SubGhzSceneStart;
    case SubGhzConfigSceneReceiver:
        return SubGhzSceneReceiver;
    case SubGhzConfigSceneSaved:
        return SubGhzSceneSaved;
    case SubGhzConfigSceneSaveName:
        return SubGhzSceneSaveName;
    case SubGhzConfigSceneSaveSuccess:
        return SubGhzSceneSaveSuccess;
    case SubGhzConfigSceneShowError:
        return SubGhzSceneShowError;
    case SubGhzConfigSceneShowErrorSub:
        return SubGhzSceneShowErrorSub;
    case SubGhzConfigSceneTransmitter:
        return SubGhzSceneTransmitter;
    case SubGhzConfigSceneDecodeRAW:
        return SubGhzSceneDecodeRAW;
    case SubGhzConfigSceneReceiverInfo:
        return SubGhzSceneReceiverInfo;
    case SubGhzConfigSceneDeleteSuccess:
        return SubGhzSceneDeleteSuccess;
    case SubGhzConfigSceneReadRAW:
        return SubGhzSceneReadRAW;
    case SubGhzConfigSceneDeleteRAW:
        return SubGhzSceneDeleteRAW;
    case SubGhzConfigSceneNone:
    default:
        return SubGhzSceneStart;
    }
}

static Widget* host_get_widget(void* ctx) {
    return ((SubGhz*)ctx)->widget;
}
static Popup* host_get_popup(void* ctx) {
    return ((SubGhz*)ctx)->popup;
}
static TextInput* host_get_text_input(void* ctx) {
    return ((SubGhz*)ctx)->text_input;
}
static ByteInput* host_get_byte_input(void* ctx) {
    return ((SubGhz*)ctx)->byte_input;
}
static Submenu* host_get_submenu(void* ctx) {
    return ((SubGhz*)ctx)->submenu;
}
static void host_switch_to_view(void* ctx, SubGhzConfigView view) {
    SubGhz* subghz = ctx;
    uint32_t vid;
    switch(view) {
    case SubGhzConfigViewPopup:
        vid = SubGhzViewIdPopup;
        break;
    case SubGhzConfigViewTextInput:
        vid = SubGhzViewIdTextInput;
        break;
    case SubGhzConfigViewByteInput:
        vid = SubGhzViewIdByteInput;
        break;
    case SubGhzConfigViewSubmenu:
        vid = SubGhzViewIdMenu;
        break;
    case SubGhzConfigViewWidget:
    default:
        vid = SubGhzViewIdWidget;
        break;
    }
    view_dispatcher_switch_to_view(subghz->view_dispatcher, vid);
}
static void host_widget_add_fw_icon(void* ctx, uint8_t x, uint8_t y, SubGhzConfigIcon icon) {
    SubGhz* subghz = ctx;
    UNUSED(icon);
    widget_add_icon_element(subghz->widget, x, y, &I_SDQuestion_35x43);
}
static uint32_t host_get_scene_state(void* ctx) {
    SubGhz* subghz = ctx;
    return scene_manager_get_scene_state(subghz->scene_manager, s_active.scene_id);
}
static void host_set_scene_state_of(void* ctx, SubGhzConfigSceneId scene, uint32_t state) {
    SubGhz* subghz = ctx;
    scene_manager_set_scene_state(subghz->scene_manager, subghz_config_map_scene(scene), state);
}
static bool host_has_previous_scene(void* ctx, SubGhzConfigSceneId scene) {
    SubGhz* subghz = ctx;
    return scene_manager_has_previous_scene(
        subghz->scene_manager, subghz_config_map_scene(scene));
}
static FuriString* host_get_error_str(void* ctx) {
    return ((SubGhz*)ctx)->error_str;
}
static void host_set_error_str(void* ctx, const char* text) {
    furi_string_set(((SubGhz*)ctx)->error_str, text);
}
static void host_notify_sd_error(void* ctx) {
    notification_message(((SubGhz*)ctx)->notifications, &subghz_config_seq_sd_error);
}
static void host_notify_reset_rgb(void* ctx) {
    notification_message(((SubGhz*)ctx)->notifications, &sequence_reset_rgb);
}
static FuriString* host_get_file_path(void* ctx) {
    return ((SubGhz*)ctx)->file_path;
}
static char* host_get_file_name_tmp(void* ctx) {
    return ((SubGhz*)ctx)->file_name_tmp;
}
static void host_nav(void* ctx, SubGhzConfigNavKind kind, SubGhzConfigSceneId target) {

    UNUSED(ctx);
    s_active.pending_nav2_kind = (uint8_t)kind + 1u;
    s_active.pending_nav2_target = (uint32_t)target;
}

static void host_notify_set_red(void* ctx) {
    notification_message(((SubGhz*)ctx)->notifications, &sequence_set_red_255);
}
static void host_get_freq_mod(void* ctx, FuriString* freq, FuriString* mod) {
    subghz_txrx_get_frequency_and_modulation(((SubGhz*)ctx)->txrx, freq, mod, false);
}
static void host_get_decoder_string(void* ctx, FuriString* out) {
    subghz_protocol_decoder_base_get_string(
        subghz_txrx_get_decoder(((SubGhz*)ctx)->txrx), out);
}
static FuriString* host_get_file_path_tmp(void* ctx) {
    return ((SubGhz*)ctx)->file_path_tmp;
}
static bool host_delete_file(void* ctx) {
    return subghz_delete_file((SubGhz*)ctx);
}

static SubGhzConfigRxKeyState host_get_rx_key_state(void* ctx) {
    switch(subghz_rx_key_state_get((SubGhz*)ctx)) {
    case SubGhzRxKeyStateBack:
        return SubGhzConfigRxKeyStateBack;
    case SubGhzRxKeyStateExit:
        return SubGhzConfigRxKeyStateExit;
    case SubGhzRxKeyStateRAWLoad:
        return SubGhzConfigRxKeyStateRAWLoad;
    default:
        return SubGhzConfigRxKeyStateIDLE;
    }
}
static void host_set_rx_key_state(void* ctx, SubGhzConfigRxKeyState state) {
    SubGhzRxKeyState s;
    switch(state) {
    case SubGhzConfigRxKeyStateBack:
        s = SubGhzRxKeyStateBack;
        break;
    case SubGhzConfigRxKeyStateExit:
        s = SubGhzRxKeyStateExit;
        break;
    case SubGhzConfigRxKeyStateRAWLoad:
        s = SubGhzRxKeyStateRAWLoad;
        break;
    case SubGhzConfigRxKeyStateIDLE:
    default:
        s = SubGhzRxKeyStateIDLE;
        break;
    }
    subghz_rx_key_state_set((SubGhz*)ctx, s);
}
static void host_reset_preset_am650(void* ctx) {
    SubGhz* subghz = ctx;
    subghz_txrx_set_preset(subghz->txrx, "AM650", subghz->last_settings->frequency, NULL, 0);
}
static void host_receiver_release(void* ctx) {
    subghz_txrx_receiver_release(((SubGhz*)ctx)->txrx);
}
static size_t host_protocol_count(void* ctx) {
    UNUSED(ctx);
    return subghz_protocol_registry_count(&subghz_protocol_registry);
}
static const char* host_protocol_name(void* ctx, size_t index) {
    UNUSED(ctx);
    const SubGhzProtocol* p =
        subghz_protocol_registry_get_by_index(&subghz_protocol_registry, index);
    return p ? p->name : NULL;
}
static bool host_protocol_filter_is_enabled(void* ctx, size_t index) {
    return subghz_protocol_filter_is_enabled(((SubGhz*)ctx)->protocol_filter, index);
}
static void host_protocol_filter_set_enabled(void* ctx, size_t index, bool enabled) {
    subghz_protocol_filter_set_enabled(((SubGhz*)ctx)->protocol_filter, index, enabled);
}
static size_t host_protocol_filter_enabled_count(void* ctx, size_t total) {
    return subghz_protocol_filter_enabled_count(((SubGhz*)ctx)->protocol_filter, total);
}
static void host_protocol_filter_save(void* ctx) {
    subghz_protocol_filter_save(((SubGhz*)ctx)->protocol_filter);
}

static bool host_freq_is_valid(void* ctx, uint32_t hz) {
    UNUSED(ctx);
    return furi_hal_subghz_is_frequency_valid(hz);
}

static bool host_file_available(void* ctx) {
    return subghz_file_available((SubGhz*)ctx);
}
static bool host_raw_edit_available(void* ctx) {
    UNUSED(ctx);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool present = storage_file_exists(storage, SUBGHZ_RAW_EDIT_FAP_PATH);
    furi_record_close(RECORD_STORAGE);
    return present;
}
static void host_launch_raw_edit(void* ctx) {
    SubGhz* subghz = ctx;

    Storage* raw_storage = furi_record_open(RECORD_STORAGE);
    File* raw_f = storage_file_alloc(raw_storage);
    if(storage_file_open(raw_f, "/ext/subghz/.focus_file", FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        const char* fp = furi_string_get_cstr(subghz->file_path);
        const char* prefix = "rawreturn:";
        storage_file_write(raw_f, prefix, strlen(prefix));
        storage_file_write(raw_f, fp, strlen(fp));
    }
    storage_file_close(raw_f);
    storage_file_free(raw_f);
    furi_record_close(RECORD_STORAGE);

    if(!subghz->blank_transition_viewport) {
        subghz->blank_transition_viewport = view_port_alloc();
        view_port_draw_callback_set(
            subghz->blank_transition_viewport, subghz_blank_transition_draw_cb, NULL);
        gui_add_view_port(subghz->gui, subghz->blank_transition_viewport, GuiLayerFullscreen);
        view_port_update(subghz->blank_transition_viewport);
    }

    Loader* loader = furi_record_open(RECORD_LOADER);
    loader_enqueue_launch(
        loader,
        SUBGHZ_RAW_EDIT_FAP_PATH,
        furi_string_get_cstr(subghz->file_path),
        LoaderDeferredLaunchFlagNone);
    furi_record_close(RECORD_LOADER);

    scene_manager_stop(subghz->scene_manager);
    view_dispatcher_stop(subghz->view_dispatcher);
}

static void subghz_config_fill_api(SubGhz* subghz) {
    SubGhzConfigHostApi* a = &s_active.api;
    a->ctx = subghz;
    a->get_setting = host_get_setting;
    a->get_item_list = host_get_item_list;
    a->get_last_settings = host_get_last_settings;
    a->switch_to_item_list_view = host_switch_to_item_list_view;
    a->save_all = host_save_all;
    a->get_view_dispatcher = host_get_view_dispatcher;
    a->set_scene_state = host_set_scene_state;
    a->open_scene = host_open_scene;
    a->mod_filter_is_enabled = host_mod_filter_is_enabled;
    a->mod_filter_set_enabled = host_mod_filter_set_enabled;
    a->set_freq_offset_hw = host_set_freq_offset_hw;
    a->device_external_available = host_device_external_available;
    a->device_get_index = host_device_get_index;
    a->device_set_index = host_device_set_index;
    a->get_tx_power = host_get_tx_power;
    a->set_tx_power = host_set_tx_power;
    a->set_debug_pin_state = host_set_debug_pin_state;
    a->get_debug_pin_state = host_get_debug_pin_state;
    a->threshold_rssi_set = host_threshold_rssi_set;
    a->threshold_rssi_get = host_threshold_rssi_get;
    a->reseat_radio_device = host_reseat_radio_device;
    a->set_ext_leds_and_amp = host_set_ext_leds_and_amp;
    a->set_dangerous_frequency = host_set_dangerous_frequency;
    a->set_rolling_counter_mult = host_set_rolling_counter_mult;
    a->get_rolling_counter_mult = host_get_rolling_counter_mult;
    a->is_debug_mode = host_is_debug_mode;
    a->is_read_raw_mode = host_is_read_raw_mode;
    a->clear_read_raw_state = host_clear_read_raw_state;
    a->previous_scene = host_previous_scene;
    a->lock_keyboard = host_lock_keyboard;
    a->hopper_get_running = host_hopper_get_running;
    a->hopper_set_running = host_hopper_set_running;
    a->preset_hopper_get_running = host_preset_hopper_get_running;
    a->preset_hopper_set_running = host_preset_hopper_set_running;
    a->preset_hopper_pause = host_preset_hopper_pause;
    a->speaker_get_enabled = host_speaker_get_enabled;
    a->speaker_set_enabled = host_speaker_set_enabled;
    a->get_ignore_filter = host_get_ignore_filter;
    a->set_ignore_filter = host_set_ignore_filter;
    a->get_filter = host_get_filter;
    a->set_filter = host_set_filter;
    a->get_preset_frequency = host_get_preset_frequency;
    a->current_preset_index = host_current_preset_index;
    a->current_preset_name = host_current_preset_name;
    a->predefined_count = host_predefined_count;
    a->predefined_get_name = host_predefined_get_name;
    a->apply_frequency = host_apply_frequency;
    a->apply_preset_at_frequency = host_apply_preset_at_frequency;
    a->apply_preset = host_apply_preset;
    a->apply_predefined = host_apply_predefined;
    a->reset_to_default = host_reset_to_default;
    a->sync_preset_to_fa = host_sync_preset_to_fa;

    a->get_widget = host_get_widget;
    a->get_popup = host_get_popup;
    a->get_text_input = host_get_text_input;
    a->get_byte_input = host_get_byte_input;
    a->get_submenu = host_get_submenu;
    a->switch_to_view = host_switch_to_view;
    a->widget_add_fw_icon = host_widget_add_fw_icon;
    a->get_scene_state = host_get_scene_state;
    a->set_scene_state_of = host_set_scene_state_of;
    a->has_previous_scene = host_has_previous_scene;
    a->get_error_str = host_get_error_str;
    a->set_error_str = host_set_error_str;
    a->notify_sd_error = host_notify_sd_error;
    a->notify_reset_rgb = host_notify_reset_rgb;
    a->get_file_path = host_get_file_path;
    a->get_file_name_tmp = host_get_file_name_tmp;
    a->nav = host_nav;

    a->notify_set_red = host_notify_set_red;
    a->get_freq_mod = host_get_freq_mod;
    a->get_decoder_string = host_get_decoder_string;
    a->get_file_path_tmp = host_get_file_path_tmp;
    a->delete_file = host_delete_file;

    a->get_rx_key_state = host_get_rx_key_state;
    a->set_rx_key_state = host_set_rx_key_state;
    a->reset_preset_am650 = host_reset_preset_am650;
    a->receiver_release = host_receiver_release;
    a->protocol_count = host_protocol_count;
    a->protocol_name = host_protocol_name;
    a->protocol_filter_is_enabled = host_protocol_filter_is_enabled;
    a->protocol_filter_set_enabled = host_protocol_filter_set_enabled;
    a->protocol_filter_enabled_count = host_protocol_filter_enabled_count;
    a->protocol_filter_save = host_protocol_filter_save;

    a->freq_is_valid = host_freq_is_valid;

    a->file_available = host_file_available;
    a->raw_edit_available = host_raw_edit_available;
    a->launch_raw_edit = host_launch_raw_edit;
}

void subghz_config_scene_loader_flush(void) {
    if(s_active.manager) plugin_manager_free(s_active.manager);
    s_active.manager = NULL;
    s_active.plugin = NULL;
    s_active.fal[0] = '\0';
}

void subghz_config_scene_enter(void* context, const char* fal_base, uint32_t scene_id) {
    SubGhz* subghz = context;
    s_active.scene_id = scene_id;
    s_active.pending_nav = 0;
    subghz_config_fill_api(subghz);

    bool reuse = s_active.manager && s_active.plugin && strcmp(s_active.fal, fal_base) == 0;
    if(!reuse) {
        subghz_config_scene_loader_flush();
        if(memmgr_get_free_heap() >= SUBGHZ_CONFIG_MIN_FREE_HEAP) {
            PluginManager* manager = plugin_manager_alloc(
                SUBGHZ_CONFIG_SCENE_PLUGIN_APP_ID,
                SUBGHZ_CONFIG_SCENE_PLUGIN_API_VERSION,
                firmware_api_interface);
            FuriString* path = furi_string_alloc();
            furi_string_printf(path, APP_DATA_PATH("plugins/%s.fal"), fal_base);
            PluginManagerError err =
                plugin_manager_load_single(manager, furi_string_get_cstr(path));
            furi_string_free(path);
            const SubGhzConfigScenePlugin* loaded = NULL;
            if(err == PluginManagerErrorNone) {
                loaded = plugin_manager_get_ep(manager, 0);
            }
            if(loaded && loaded->set_host_api && loaded->on_enter) {
                s_active.manager = manager;
                s_active.plugin = loaded;
                strncpy(s_active.fal, fal_base, sizeof(s_active.fal) - 1);
                s_active.fal[sizeof(s_active.fal) - 1] = '\0';
            } else {
                plugin_manager_free(manager);
            }
        }
    }

    if(s_active.plugin) {
        s_active.plugin->set_host_api(&s_active.api);
        s_active.plugin->on_enter();
    } else {
        FURI_LOG_E(TAG, "config FAL %s unavailable", fal_base);
        variable_item_list_add(subghz->variable_item_list, "Config plugin missing", 1, NULL, NULL);
        view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdVariableItemList);
    }
}

bool subghz_config_scene_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    if(!s_active.plugin || !s_active.plugin->on_event) return false;
    s_active.pending_nav = 0;
    s_active.pending_prev = false;
    s_active.pending_nav2_kind = 0;
    bool consumed = s_active.plugin->on_event(event);
    if(s_active.pending_prev) {
        s_active.pending_prev = false;
        scene_manager_previous_scene(subghz->scene_manager);
    } else if(s_active.pending_nav) {
        SubGhzConfigNavTarget target = (SubGhzConfigNavTarget)(s_active.pending_nav - 1u);
        s_active.pending_nav = 0;
        uint32_t id =
            (target == SubGhzConfigNavCustomFreq) ? SubGhzSceneCustomFreq : SubGhzSceneFilePrefix;
        s_active.nav_child = true;
        scene_manager_next_scene(subghz->scene_manager, id);
    } else if(s_active.pending_nav2_kind) {

        SubGhzConfigNavKind kind = (SubGhzConfigNavKind)(s_active.pending_nav2_kind - 1u);
        uint32_t target =
            subghz_config_map_scene((SubGhzConfigSceneId)s_active.pending_nav2_target);
        s_active.pending_nav2_kind = 0;
        switch(kind) {
        case SubGhzConfigNavPrevious:
            scene_manager_previous_scene(subghz->scene_manager);
            break;
        case SubGhzConfigNavNext:
            scene_manager_next_scene(subghz->scene_manager, target);
            break;
        case SubGhzConfigNavSearchPrev:
            scene_manager_search_and_switch_to_previous_scene(subghz->scene_manager, target);
            break;
        case SubGhzConfigNavStop:
            scene_manager_stop(subghz->scene_manager);
            view_dispatcher_stop(subghz->view_dispatcher);
            break;
        case SubGhzConfigNavPreviousOrStop:
            if(!scene_manager_previous_scene(subghz->scene_manager)) {
                scene_manager_stop(subghz->scene_manager);
                view_dispatcher_stop(subghz->view_dispatcher);
            }
            break;
        case SubGhzConfigNavSearchPrevOrStart:
            if(!scene_manager_search_and_switch_to_previous_scene(
                   subghz->scene_manager, target)) {
                scene_manager_search_and_switch_to_previous_scene(
                    subghz->scene_manager, SubGhzSceneStart);
            }
            break;
        case SubGhzConfigNavSearchPrevOrPrevious:
            if(!scene_manager_search_and_switch_to_previous_scene(
                   subghz->scene_manager, target)) {
                scene_manager_previous_scene(subghz->scene_manager);
            }
            break;
        case SubGhzConfigNavSearchPrevOrStop:
            if(!scene_manager_search_and_switch_to_previous_scene(
                   subghz->scene_manager, target)) {
                scene_manager_stop(subghz->scene_manager);
                view_dispatcher_stop(subghz->view_dispatcher);
            }
            break;
        default:
            break;
        }
    }
    return consumed;
}

void subghz_config_scene_exit(void* context) {
    SubGhz* subghz = context;
    if(s_active.plugin && s_active.plugin->on_exit) {
        s_active.plugin->on_exit();
    } else {
        variable_item_list_set_selected_item(subghz->variable_item_list, 0);
        variable_item_list_reset(subghz->variable_item_list);
    }

    bool keep = s_active.nav_child;
    s_active.nav_child = false;
    if(!keep) {
        subghz_config_scene_loader_flush();
    }
}
