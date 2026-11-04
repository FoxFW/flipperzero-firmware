/* FoxFW2.0 config-externalization Stage 3 (2026-10-31).
 * Receiver config scene (the main sub-GHz Read config) as an on-demand .fal.
 * Reaches SubGhz/txrx/hopper/speaker/predefined/lock state ONLY through
 * SubGhzConfigHostApi; never includes subghz_i.h. Faithful port of the former
 * resident subghz_scene_receiver_config.c, with per-change SD saves removed
 * (settings update in RAM + radio reconfigures immediately; persisted once in
 * on_exit) to kill fast-scroll heap/SD churn. */

#include "../helpers/subghz_config_scene.h"
#include <lib/subghz/types.h>  /* SubGhzProtocolFlag_* */
#include <lib/toolbox/value_index.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static const SubGhzConfigHostApi* g_api = NULL;

/* Private custom-event codes (round-trip within this scene only). */
#define RC_EVT_LOCK  0xFA10u
#define RC_EVT_RESET 0xFA11u

enum SubGhzSettingIndex {
    SubGhzSettingIndexPredefined,
    SubGhzSettingIndexFrequency,
    SubGhzSettingIndexModulation,
    SubGhzSettingIndexRadioDevice,
    SubGhzSettingIndexCaptureUnknownRaw,
    SubGhzSettingIndexHopping,
    SubGhzSettingIndexPresetHopping,
    SubGhzSettingIndexBinRAW,
    SubGhzSettingIndexIgnoreReversRB2,
    SubGhzSettingIndexIgnoreAlarms,
    SubGhzSettingIndexIgnoreSensors,
    SubGhzSettingIndexIgnorePrinceton,
    SubGhzSettingIndexIgnoreNiceFlorS,
    SubGhzSettingIndexDeleteOldSignals,
    SubGhzSettingIndexAutoSave,
    SubGhzSettingIndexSound,
    SubGhzSettingIndexResetToDefault,
    SubGhzSettingIndexLock,
    SubGhzSettingIndexRAWThresholdRSSI,
};

#define RAW_THRESHOLD_RSSI_COUNT 11
static const char* const raw_threshold_rssi_text[RAW_THRESHOLD_RSSI_COUNT] = {
    "-----", "-85.0", "-80.0", "-75.0", "-70.0", "-65.0",
    "-60.0", "-55.0", "-50.0", "-45.0", "-40.0"};
static const float raw_threshold_rssi_value[RAW_THRESHOLD_RSSI_COUNT] = {
    -90.0f, -85.0f, -80.0f, -75.0f, -70.0f, -65.0f,
    -60.0f, -55.0f, -50.0f, -45.0f, -40.0f};

#define HOPPING_MODE_COUNT 12
static const char* const hopping_mode_text[HOPPING_MODE_COUNT] = {
    "OFF", "-90dBm", "-85dBm", "-80dBm", "-75dBm", "-70dBm",
    "-65dBm", "-60dBm", "-55dBm", "-50dBm", "-45dBm", "-40dBm"};
static const float hopping_mode_value[HOPPING_MODE_COUNT] = {
    NAN, -90.0f, -85.0f, -80.0f, -75.0f, -70.0f,
    -65.0f, -60.0f, -55.0f, -50.0f, -45.0f, -40.0f};

#define PRESET_HOPPING_MODE_COUNT 12
static const char* const preset_hopping_mode_text[PRESET_HOPPING_MODE_COUNT] = {
    "OFF", "-90dBm", "-85dBm", "-80dBm", "-75dBm", "-70dBm",
    "-65dBm", "-60dBm", "-55dBm", "-50dBm", "-45dBm", "-40dBm"};
static const float preset_hopping_mode_value[PRESET_HOPPING_MODE_COUNT] = {
    NAN, -90.0f, -85.0f, -80.0f, -75.0f, -70.0f,
    -65.0f, -60.0f, -55.0f, -50.0f, -45.0f, -40.0f};

#define COMBO_BOX_COUNT 2
static const char* const combobox_text[COMBO_BOX_COUNT] = {"OFF", "ON"};
static const uint32_t bin_raw_value[COMBO_BOX_COUNT] = {
    SubGhzProtocolFlag_Decodable,
    SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_BinRAW};

#define RECEIVER_RADIO_DEVICE_COUNT 2
static const char* const receiver_radio_device_text[RECEIVER_RADIO_DEVICE_COUNT] = {
    "Internal", "External"};

/* ---- modulation-filter display<->actual mapping (filter always allocated) ---- */
static uint8_t rc_mod_enabled_count(void) {
    SubGhzSetting* s = g_api->get_setting(g_api->ctx);
    size_t total = subghz_setting_get_preset_count(s);
    size_t n = 0;
    for(size_t i = 0; i < total; i++)
        if(g_api->mod_filter_is_enabled(g_api->ctx, i)) n++;
    return n > 0 ? (uint8_t)n : 1;
}
static uint8_t rc_mod_nth_actual(uint8_t nth) {
    SubGhzSetting* s = g_api->get_setting(g_api->ctx);
    size_t total = subghz_setting_get_preset_count(s);
    uint8_t found = 0;
    for(size_t i = 0; i < total; i++) {
        if(g_api->mod_filter_is_enabled(g_api->ctx, i)) {
            if(found == nth) return (uint8_t)i;
            found++;
        }
    }
    return 0;
}
static uint8_t rc_mod_display_idx(uint8_t actual) {
    SubGhzSetting* s = g_api->get_setting(g_api->ctx);
    size_t total = subghz_setting_get_preset_count(s);
    uint8_t display = 0;
    for(size_t i = 0; i < total; i++) {
        if(!g_api->mod_filter_is_enabled(g_api->ctx, i)) continue;
        if(i == actual) return display;
        display++;
    }
    return 0;
}

static uint8_t rc_next_frequency(uint32_t value) {
    SubGhzSetting* setting = g_api->get_setting(g_api->ctx);
    uint8_t index = 0;
    for(size_t i = 0; i < subghz_setting_get_frequency_count(setting); i++) {
        if(value == subghz_setting_get_frequency(setting, i)) {
            index = (uint8_t)i;
            break;
        } else {
            index = (uint8_t)subghz_setting_get_frequency_default_index(setting);
        }
    }
    return index;
}

static uint8_t rc_hopper_value_index(void) {
    if(!g_api->hopper_get_running(g_api->ctx)) return 0;
    VariableItemList* list = g_api->get_item_list(g_api->ctx);
    variable_item_set_current_value_text(
        variable_item_list_get(list, SubGhzSettingIndexFrequency), " -----");
    return (uint8_t)value_index_float(
        g_api->get_last_settings(g_api->ctx)->hopping_threshold,
        hopping_mode_value,
        HOPPING_MODE_COUNT);
}
static uint8_t rc_preset_hopper_value_index(void) {
    if(!g_api->preset_hopper_get_running(g_api->ctx)) return 0;
    VariableItemList* list = g_api->get_item_list(g_api->ctx);
    variable_item_set_current_value_text(
        variable_item_list_get(list, SubGhzSettingIndexModulation), " -----");
    return (uint8_t)value_index_float(
        g_api->get_last_settings(g_api->ctx)->preset_hopping_threshold,
        preset_hopping_mode_value,
        PRESET_HOPPING_MODE_COUNT);
}

/* ============================ change callbacks ============================ */
static void rc_set_predefined(VariableItem* item) {
    uint16_t index = (uint16_t)variable_item_get_current_value_index(item);
    VariableItemList* list = g_api->get_item_list(g_api->ctx);
    char name[32] = {0};
    bool active = g_api->apply_predefined(g_api->ctx, index, name, sizeof(name));
    variable_item_set_current_value_text(item, name);

    VariableItem* freq_item = variable_item_list_get(list, SubGhzSettingIndexFrequency);
    VariableItem* mod_item = variable_item_list_get(list, SubGhzSettingIndexModulation);
    if(active) {
        uint32_t freq = g_api->get_preset_frequency(g_api->ctx);
        char fbuf[10] = {0};
        snprintf(fbuf, sizeof(fbuf), "%lu.%02lu", freq / 1000000, (freq % 1000000) / 10000);
        variable_item_set_current_value_text(freq_item, fbuf);
        char pname[32] = {0};
        g_api->current_preset_name(g_api->ctx, pname, sizeof(pname));
        variable_item_set_current_value_text(mod_item, pname);
    }
    variable_item_set_locked(freq_item, active, "Turn off\nPredefined\nto do that!");
    variable_item_set_locked(mod_item, active, "Turn off\nPredefined\nto do that!");

    if(!g_api->is_read_raw_mode(g_api->ctx)) {
        VariableItem* hop_item = variable_item_list_get(list, SubGhzSettingIndexHopping);
        variable_item_set_locked(
            hop_item,
            active || g_api->preset_hopper_get_running(g_api->ctx),
            active ? "Turn off\nPredefined\nto do that!" : "Turn off\nPreset\nHopping\nfirst!");
    }
}

static void rc_set_frequency(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    SubGhzSetting* setting = g_api->get_setting(g_api->ctx);
    if(!g_api->hopper_get_running(g_api->ctx)) {
        uint32_t frequency = subghz_setting_get_frequency(setting, index);
        char text_buf[10] = {0};
        snprintf(
            text_buf, sizeof(text_buf), "%lu.%02lu", frequency / 1000000,
            (frequency % 1000000) / 10000);
        variable_item_set_current_value_text(item, text_buf);
        g_api->apply_frequency(g_api->ctx, frequency);
    } else {
        variable_item_set_current_value_index(
            item, subghz_setting_get_frequency_default_index(setting));
    }
}

static void rc_set_preset(VariableItem* item) {
    uint8_t display_idx = variable_item_get_current_value_index(item);
    uint8_t index = rc_mod_nth_actual(display_idx);
    SubGhzSetting* setting = g_api->get_setting(g_api->ctx);
    if(!g_api->preset_hopper_get_running(g_api->ctx)) {
        variable_item_set_current_value_text(item, subghz_setting_get_preset_name(setting, index));
        g_api->apply_preset(g_api->ctx, index);
    } else {
        variable_item_set_current_value_index(
            item, (uint8_t)g_api->get_last_settings(g_api->ctx)->preset_index);
    }
}

static void rc_set_device(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    uint8_t applied = g_api->device_set_index(g_api->ctx, index);
    variable_item_set_current_value_text(item, receiver_radio_device_text[applied]);
}

static void rc_set_capture_unknown_raw(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, combobox_text[index]);
    g_api->get_last_settings(g_api->ctx)->capture_unknown_raw = index == 1;
}

static void rc_set_hopping(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    SubGhzSetting* setting = g_api->get_setting(g_api->ctx);
    VariableItemList* list = g_api->get_item_list(g_api->ctx);
    VariableItem* freq_item = variable_item_list_get(list, SubGhzSettingIndexFrequency);
    SubGhzLastSettings* last = g_api->get_last_settings(g_api->ctx);

    variable_item_set_current_value_text(item, hopping_mode_text[index]);
    if(index == 0) {
        uint32_t frequency = subghz_setting_get_default_frequency(setting);
        char text_buf[10] = {0};
        snprintf(
            text_buf, sizeof(text_buf), "%lu.%02lu", frequency / 1000000,
            (frequency % 1000000) / 10000);
        variable_item_set_current_value_text(freq_item, text_buf);
        g_api->apply_preset_at_frequency(g_api->ctx, frequency);
        variable_item_set_current_value_index(
            freq_item, subghz_setting_get_frequency_default_index(setting));
        variable_item_set_item_label(item, "Hopping");
    } else {
        variable_item_set_current_value_text(freq_item, " -----");
        variable_item_set_current_value_index(
            freq_item, subghz_setting_get_frequency_default_index(setting));
        variable_item_set_item_label(item, "Hopping RSSI");
    }
    last->enable_hopping = index != 0;
    last->hopping_threshold = hopping_mode_value[index];
    g_api->hopper_set_running(g_api->ctx, index != 0);

    VariableItem* preset_hopping_item = variable_item_list_get(list, SubGhzSettingIndexPresetHopping);
    variable_item_set_locked(preset_hopping_item, index != 0, "Turn off\nHopping\nfirst!");
}

static void rc_set_preset_hopping(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    SubGhzSetting* setting = g_api->get_setting(g_api->ctx);
    VariableItemList* list = g_api->get_item_list(g_api->ctx);
    VariableItem* preset_item = variable_item_list_get(list, SubGhzSettingIndexModulation);
    SubGhzLastSettings* last = g_api->get_last_settings(g_api->ctx);

    variable_item_set_current_value_text(item, preset_hopping_mode_text[index]);
    if(index == 0) {
        char pname[32] = {0};
        g_api->current_preset_name(g_api->ctx, pname, sizeof(pname));
        int cur_idx = subghz_setting_get_inx_preset_by_name(setting, pname);
        if(cur_idx >= 0) last->preset_index = cur_idx;
        variable_item_set_current_value_text(preset_item, pname);
        variable_item_set_current_value_index(preset_item, (uint8_t)last->preset_index);
        last->enable_preset_hopping = false;
        g_api->preset_hopper_set_running(g_api->ctx, false);
    } else {
        if(g_api->preset_hopper_get_running(g_api->ctx)) {
            g_api->preset_hopper_pause(g_api->ctx);
        }
        last->preset_hopping_threshold = preset_hopping_mode_value[index];
        variable_item_set_current_value_text(preset_item, " -----");
        variable_item_set_current_value_index(preset_item, (uint8_t)last->preset_index);
        last->enable_preset_hopping = true;
        g_api->preset_hopper_set_running(g_api->ctx, true);
    }

    VariableItem* hopping_item = variable_item_list_get(list, SubGhzSettingIndexHopping);
    variable_item_set_locked(hopping_item, index != 0, "Turn off\nPreset\nHopping\nfirst!");
}

static void rc_set_speaker(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, combobox_text[index]);
    g_api->speaker_set_enabled(g_api->ctx, index == 1);
}

static void rc_set_bin_raw(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, combobox_text[index]);
    g_api->set_filter(g_api->ctx, bin_raw_value[index]);
}

static void rc_set_raw_threshold_rssi(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, raw_threshold_rssi_text[index]);
    g_api->threshold_rssi_set(g_api->ctx, raw_threshold_rssi_value[index]);
    g_api->get_last_settings(g_api->ctx)->rssi = raw_threshold_rssi_value[index];
}

static void rc_set_ignore_filter(VariableItem* item, SubGhzProtocolFlag flag) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, combobox_text[index]);
    uint32_t f = g_api->get_ignore_filter(g_api->ctx);
    if(index == 0)
        f &= ~(uint32_t)flag;
    else
        f |= (uint32_t)flag;
    g_api->set_ignore_filter(g_api->ctx, f);
}
static void rc_set_reversrb2(VariableItem* item) {
    rc_set_ignore_filter(item, SubGhzProtocolFlag_ReversRB2);
}
static void rc_set_alarms(VariableItem* item) {
    rc_set_ignore_filter(item, SubGhzProtocolFlag_Alarms);
}
static void rc_set_sensors(VariableItem* item) {
    rc_set_ignore_filter(item, SubGhzProtocolFlag_Sensors);
}
static void rc_set_princeton(VariableItem* item) {
    rc_set_ignore_filter(item, SubGhzProtocolFlag_Princeton);
}
static void rc_set_niceflors(VariableItem* item) {
    rc_set_ignore_filter(item, SubGhzProtocolFlag_NiceFlorS);
}

static void rc_set_delete_old_signals(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, combobox_text[index]);
    g_api->get_last_settings(g_api->ctx)->delete_old_signals = index == 1;
}
static void rc_set_auto_save(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, combobox_text[index]);
    g_api->get_last_settings(g_api->ctx)->auto_save = index == 1;
}

static bool rc_ignore_filter_get_index(uint32_t filter, SubGhzProtocolFlag flag) {
    return (filter & (uint32_t)flag) > 0;
}

static void rc_enter_cb(void* context, uint32_t index) {
    UNUSED(context);
    if(index == SubGhzSettingIndexLock) {
        view_dispatcher_send_custom_event(g_api->get_view_dispatcher(g_api->ctx), RC_EVT_LOCK);
    } else if(index == SubGhzSettingIndexResetToDefault) {
        g_api->reset_to_default(g_api->ctx);
        VariableItemList* list = g_api->get_item_list(g_api->ctx);
        variable_item_list_set_selected_item(list, 0);
        variable_item_list_reset(list);
        view_dispatcher_send_custom_event(g_api->get_view_dispatcher(g_api->ctx), RC_EVT_RESET);
    }
}

/* ================================ lifecycle ================================ */
static void rc_plugin_set_host_api(const SubGhzConfigHostApi* api) {
    g_api = api;
}

static void rc_plugin_on_enter(void) {
    VariableItemList* list = g_api->get_item_list(g_api->ctx);
    SubGhzSetting* setting = g_api->get_setting(g_api->ctx);
    SubGhzLastSettings* last = g_api->get_last_settings(g_api->ctx);
    VariableItem* item;
    uint8_t value_index;
    bool read_raw = g_api->is_read_raw_mode(g_api->ctx);

    uint16_t pdef_count = g_api->predefined_count(g_api->ctx);
    {
        uint16_t pidx = (uint16_t)last->predefined_index;
        if(pidx > pdef_count) pidx = 0;
        char name[32] = {0};
        g_api->predefined_get_name(g_api->ctx, pidx, name, sizeof(name));
        item = variable_item_list_add(
            list, "Select Predefined", pdef_count + 1, rc_set_predefined, NULL);
        variable_item_set_current_value_index(item, (uint8_t)pidx);
        variable_item_set_current_value_text(item, name);
        variable_item_set_centered(item, true);
    }

    item = variable_item_list_add(
        list, "Frequency", subghz_setting_get_frequency_count(setting), rc_set_frequency, NULL);
    value_index = rc_next_frequency(g_api->get_preset_frequency(g_api->ctx));
    g_api->set_scene_state(g_api->ctx, (uint32_t)(uintptr_t)item);
    variable_item_set_current_value_index(item, value_index);
    {
        char text_buf[10] = {0};
        uint32_t frequency = subghz_setting_get_frequency(setting, value_index);
        snprintf(
            text_buf, sizeof(text_buf), "%lu.%02lu", frequency / 1000000,
            (frequency % 1000000) / 10000);
        variable_item_set_current_value_text(item, text_buf);
    }
    variable_item_set_locked(
        item, last->predefined_index != 0, "Turn off\nPredefined\nto do that!");

    item = variable_item_list_add(
        list, "Modulation", rc_mod_enabled_count(), rc_set_preset, NULL);
    {
        uint8_t actual = g_api->current_preset_index(g_api->ctx);
        if(!g_api->mod_filter_is_enabled(g_api->ctx, actual)) actual = rc_mod_nth_actual(0);
        variable_item_set_current_value_index(item, rc_mod_display_idx(actual));
        variable_item_set_current_value_text(item, subghz_setting_get_preset_name(setting, actual));
    }
    variable_item_set_locked(
        item, last->predefined_index != 0, "Turn off\nPredefined\nto do that!");

    uint8_t rc_device_count = RECEIVER_RADIO_DEVICE_COUNT;
    if(g_api->device_get_index(g_api->ctx) == 0 && !g_api->device_external_available(g_api->ctx))
        rc_device_count = 1;
    item = variable_item_list_add(list, "Module", rc_device_count, rc_set_device, NULL);
    value_index = g_api->device_get_index(g_api->ctx);
    if(value_index >= rc_device_count) value_index = 0;
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, receiver_radio_device_text[value_index]);

    item = variable_item_list_add(
        list, "Unknown RAW", COMBO_BOX_COUNT, rc_set_capture_unknown_raw, NULL);
    value_index = last->capture_unknown_raw ? 1 : 0;
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, combobox_text[value_index]);

    if(!read_raw) {
        value_index = rc_hopper_value_index();
        item = variable_item_list_add(
            list, value_index ? "Hopping RSSI" : "Hopping", HOPPING_MODE_COUNT, rc_set_hopping,
            NULL);
        variable_item_set_current_value_index(item, value_index);
        variable_item_set_current_value_text(item, hopping_mode_text[value_index]);
        variable_item_set_locked(
            item,
            last->predefined_index != 0 || g_api->preset_hopper_get_running(g_api->ctx),
            last->predefined_index != 0 ? "Turn off\nPredefined\nto do that!" :
                                          "Turn off\nPreset\nHopping\nfirst!");

        value_index = rc_preset_hopper_value_index();
        item = variable_item_list_add(
            list, "Preset Hopping", PRESET_HOPPING_MODE_COUNT, rc_set_preset_hopping, NULL);
        variable_item_set_current_value_index(item, value_index);
        variable_item_set_current_value_text(item, preset_hopping_mode_text[value_index]);
        variable_item_set_locked(
            item, g_api->hopper_get_running(g_api->ctx), "Turn off\nHopping\nfirst!");
    }

    item = variable_item_list_add(
        list, "RSSI Threshold:", RAW_THRESHOLD_RSSI_COUNT, rc_set_raw_threshold_rssi, NULL);
    value_index = (uint8_t)value_index_float(
        g_api->threshold_rssi_get(g_api->ctx), raw_threshold_rssi_value, RAW_THRESHOLD_RSSI_COUNT);
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, raw_threshold_rssi_text[value_index]);

    if(!read_raw) {
        item = variable_item_list_add(list, "Bin RAW", COMBO_BOX_COUNT, rc_set_bin_raw, NULL);
        value_index =
            (uint8_t)value_index_uint32(g_api->get_filter(g_api->ctx), bin_raw_value, COMBO_BOX_COUNT);
        variable_item_set_current_value_index(item, value_index);
        variable_item_set_current_value_text(item, combobox_text[value_index]);
    }

    if(!read_raw) {
        uint32_t ig = g_api->get_ignore_filter(g_api->ctx);
        struct {
            const char* label;
            VariableItemChangeCallback cb;
            SubGhzProtocolFlag flag;
        } filters[] = {
            {"Ignore ReversRB2", rc_set_reversrb2, SubGhzProtocolFlag_ReversRB2},
            {"Ignore Alarms", rc_set_alarms, SubGhzProtocolFlag_Alarms},
            {"Ignore Sensors", rc_set_sensors, SubGhzProtocolFlag_Sensors},
            {"Ignore Princeton", rc_set_princeton, SubGhzProtocolFlag_Princeton},
            {"Ignore Nice Flor-S / Nice One", rc_set_niceflors, SubGhzProtocolFlag_NiceFlorS},
        };
        for(size_t i = 0; i < 5; i++) {
            item = variable_item_list_add(list, filters[i].label, COMBO_BOX_COUNT, filters[i].cb, NULL);
            value_index = rc_ignore_filter_get_index(ig, filters[i].flag) ? 1 : 0;
            variable_item_set_current_value_index(item, value_index);
            variable_item_set_current_value_text(item, combobox_text[value_index]);
        }

        item = variable_item_list_add(
            list, "Delete old signals when memory is full", COMBO_BOX_COUNT,
            rc_set_delete_old_signals, NULL);
        value_index = last->delete_old_signals ? 1 : 0;
        variable_item_set_current_value_index(item, value_index);
        variable_item_set_current_value_text(item, combobox_text[value_index]);

        item = variable_item_list_add(
            list, "Auto-Save Every Signal", COMBO_BOX_COUNT, rc_set_auto_save, NULL);
        value_index = last->auto_save ? 1 : 0;
        variable_item_set_current_value_index(item, value_index);
        variable_item_set_current_value_text(item, combobox_text[value_index]);
    }

    item = variable_item_list_add(list, "Sound", COMBO_BOX_COUNT, rc_set_speaker, NULL);
    value_index = g_api->speaker_get_enabled(g_api->ctx) ? 1 : 0;
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, combobox_text[value_index]);

    if(!read_raw) {
        variable_item_list_add(list, "Reset to default", 1, NULL, NULL);
        variable_item_list_add(list, "Lock Keyboard", 1, NULL, NULL);
        variable_item_list_set_enter_callback(list, rc_enter_cb, NULL);
    }

    g_api->switch_to_item_list_view(g_api->ctx);
}

static bool rc_plugin_on_event(SceneManagerEvent event) {
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == RC_EVT_LOCK) {
            g_api->lock_keyboard(g_api->ctx);
            g_api->previous_scene(g_api->ctx);
            return true;
        } else if(event.event == RC_EVT_RESET) {
            g_api->previous_scene(g_api->ctx);
            return true;
        }
    }
    return false;
}

static void rc_plugin_on_exit(void) {
    VariableItemList* list = g_api->get_item_list(g_api->ctx);
    variable_item_list_set_selected_item(list, 0);
    variable_item_list_reset(list);
    g_api->save_all(g_api->ctx);
    g_api->sync_preset_to_fa(g_api->ctx, g_api->get_last_settings(g_api->ctx)->preset_index);
    g_api->clear_read_raw_state(g_api->ctx);
}

static const SubGhzConfigScenePlugin rc_plugin = {
    .set_host_api = rc_plugin_set_host_api,
    .on_enter = rc_plugin_on_enter,
    .on_event = rc_plugin_on_event,
    .on_exit = rc_plugin_on_exit,
};

static const FlipperAppPluginDescriptor rc_plugin_descriptor = {
    .appid = SUBGHZ_CONFIG_SCENE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_CONFIG_SCENE_PLUGIN_API_VERSION,
    .entry_point = &rc_plugin,
};

const FlipperAppPluginDescriptor* subghz_config_receiver_ep(void) {
    return &rc_plugin_descriptor;
}
