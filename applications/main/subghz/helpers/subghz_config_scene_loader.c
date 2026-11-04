#include "subghz_config_scene_loader.h"
#include "subghz_config_scene.h"
#include "../subghz_i.h"
#include <applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.h>
#include <string.h>
#include <targets/f7/furi_hal/furi_hal_subghz.h>

#include <furi.h>
#include <flipper_application/plugins/plugin_manager.h>
#include <loader/firmware_api/firmware_api.h>

#define TAG "SubGhzConfigLoader"
#define SUBGHZ_CONFIG_MIN_FREE_HEAP 8192u

typedef struct {
    PluginManager* manager;
    const SubGhzConfigScenePlugin* plugin;
    SubGhzConfigHostApi api;
    uint32_t scene_id;
    uint8_t pending_nav; /* 0 = none; else SubGhzConfigNavTarget + 1 (deferred) */
    bool pending_prev;   /* deferred scene_manager_previous_scene (lock/reset) */
    bool nav_child;      /* this exit is a hop to a child scene -> keep FAL loaded */
    char fal[40];        /* currently-loaded FAL base name ("" = none loaded) */
} SubGhzConfigActive;

static SubGhzConfigActive s_active = {0};

/* ===== Host-API backing: resident, derefs SubGhz / furi_hal. FALs never do. ===== */
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
    /* Deferred: record intent only. The resident loader performs the real
     * scene_manager_next_scene AFTER the FAL on_event returns, so the FAL is
     * never freed (by the synchronous on_exit that next_scene triggers) while
     * its code is still on the call stack. */
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

/* ---- v3 (receiver_config) ---- */
#define RC_LAST_SETTINGS_FILE EXT_PATH("subghz/assets/last_subghz.settings")
#define RC_FA_PRESET_IDX_KEY  "FAPresetIndex"

/* Port of receiver_config's static subghz_config_sync_preset_to_fa (self-contained). */
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
    /* Deferred: scene_manager_previous_scene calls on_exit synchronously, which would
     * free this FAL while its on_event is still on the stack. Record it; the loader
     * performs it after on_event returns. */
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
    /* Matches set_hopping's OFF path: re-apply current preset at freq WITHOUT touching
     * last_settings->frequency or set_default_frequency. */
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
    subghz->last_settings->capture_unknown_raw = true;
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
}

/* Free the cached config FAL. Public: called from subghz_free() on app exit so the
 * ELF sections are released while the rest of the subghz heap is being torn down
 * (never mid-session), leaving the heap clean for the desktop return. */
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
    subghz_config_fill_api(subghz); /* refresh ctx every enter -- valid even on reuse */

    /* Reuse the FAL only if it is still loaded (we are returning from one of this
     * scene's child scenes, which kept it loaded) and it is the same one -- so child
     * round trips do NOT unload/reload (no churn). On a fresh config entry it was
     * unloaded on the previous real exit, so this loads it. */
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
    bool consumed = s_active.plugin->on_event(event);
    if(s_active.pending_prev) {
        s_active.pending_prev = false;
        scene_manager_previous_scene(subghz->scene_manager);
    } else if(s_active.pending_nav) {
        SubGhzConfigNavTarget target = (SubGhzConfigNavTarget)(s_active.pending_nav - 1u);
        s_active.pending_nav = 0;
        uint32_t id =
            (target == SubGhzConfigNavCustomFreq) ? SubGhzSceneCustomFreq : SubGhzSceneFilePrefix;
        s_active.nav_child = true; /* keep the FAL loaded across this child-scene hop */
        scene_manager_next_scene(subghz->scene_manager, id);
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
    /* Unload on a real config exit (Back to the receiver) so the FAL is NOT resident
     * during Read/decode, where RAM is tight. Keep it loaded only across a hop to one
     * of this scene's own child scenes (File Prefix / Custom Freq), so those round
     * trips do not churn/fragment the heap. Read is always reached via the receiver,
     * i.e. after this unload, so the config FAL is never in RAM while protocols load. */
    bool keep = s_active.nav_child;
    s_active.nav_child = false;
    if(!keep) {
        subghz_config_scene_loader_flush();
    }
}
