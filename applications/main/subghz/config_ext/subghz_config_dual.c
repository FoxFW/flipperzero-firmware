

#include "../helpers/subghz_config_scene.h"
#include <stdio.h>

static const SubGhzConfigHostApi* g_api = NULL;

static uint8_t dual_frequency_index(SubGhzSetting* setting, uint32_t frequency) {
    for(size_t i = 0; i < subghz_setting_get_frequency_count(setting); i++) {
        if(subghz_setting_get_frequency(setting, i) == frequency) return (uint8_t)i;
    }
    return (uint8_t)subghz_setting_get_frequency_default_index(setting);
}

static void dual_frequency_text(VariableItem* item, uint32_t frequency) {
    char text[10];
    snprintf(
        text, sizeof(text), "%lu.%02lu", frequency / 1000000 % 1000, frequency / 10000 % 100);
    variable_item_set_current_value_text(item, text);
}

static void dual_set_frequency(VariableItem* item, uint32_t* target) {
    const SubGhzConfigHostApi* api = variable_item_get_context(item);
    SubGhzSetting* setting = api->get_setting(api->ctx);
    *target = subghz_setting_get_frequency(setting, variable_item_get_current_value_index(item));
    dual_frequency_text(item, *target);
}

static void dual_set_modulation(VariableItem* item, uint32_t* target) {
    const SubGhzConfigHostApi* api = variable_item_get_context(item);
    SubGhzSetting* setting = api->get_setting(api->ctx);
    *target = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, subghz_setting_get_preset_name(setting, *target));
}

static void dual_int_frequency(VariableItem* item) {
    const SubGhzConfigHostApi* api = variable_item_get_context(item);
    dual_set_frequency(item, &api->get_last_settings(api->ctx)->dual_int_frequency);
}
static void dual_int_modulation(VariableItem* item) {
    const SubGhzConfigHostApi* api = variable_item_get_context(item);
    dual_set_modulation(item, &api->get_last_settings(api->ctx)->dual_int_preset_index);
}
static void dual_ext_frequency(VariableItem* item) {
    const SubGhzConfigHostApi* api = variable_item_get_context(item);
    dual_set_frequency(item, &api->get_last_settings(api->ctx)->dual_ext_frequency);
}
static void dual_ext_modulation(VariableItem* item) {
    const SubGhzConfigHostApi* api = variable_item_get_context(item);
    dual_set_modulation(item, &api->get_last_settings(api->ctx)->dual_ext_preset_index);
}

static void dual_int_jam_frequency(VariableItem* item) {
    const SubGhzConfigHostApi* api = variable_item_get_context(item);
    dual_set_frequency(item, &api->get_last_settings(api->ctx)->dual_int_jam_frequency);
}
static void dual_int_jam_modulation(VariableItem* item) {
    const SubGhzConfigHostApi* api = variable_item_get_context(item);
    dual_set_modulation(item, &api->get_last_settings(api->ctx)->dual_int_jam_preset_index);
}
static void dual_ext_jam_frequency(VariableItem* item) {
    const SubGhzConfigHostApi* api = variable_item_get_context(item);
    dual_set_frequency(item, &api->get_last_settings(api->ctx)->dual_ext_jam_frequency);
}
static void dual_ext_jam_modulation(VariableItem* item) {
    const SubGhzConfigHostApi* api = variable_item_get_context(item);
    dual_set_modulation(item, &api->get_last_settings(api->ctx)->dual_ext_jam_preset_index);
}

static void dual_add_frequency(const char* label, uint32_t frequency, VariableItemChangeCallback cb) {
    SubGhzSetting* setting = g_api->get_setting(g_api->ctx);
    VariableItem* item = variable_item_list_add(
        g_api->get_item_list(g_api->ctx),
        label,
        subghz_setting_get_frequency_count(setting),
        cb,
        (void*)g_api);
    const uint8_t index = dual_frequency_index(setting, frequency);
    variable_item_set_current_value_index(item, index);
    dual_frequency_text(item, subghz_setting_get_frequency(setting, index));
}

static void dual_add_modulation(const char* label, uint32_t preset_index, VariableItemChangeCallback cb) {
    SubGhzSetting* setting = g_api->get_setting(g_api->ctx);
    const size_t count = subghz_setting_get_preset_count(setting);
    VariableItem* item =
        variable_item_list_add(g_api->get_item_list(g_api->ctx), label, count, cb, (void*)g_api);
    const uint8_t index = preset_index < count ? (uint8_t)preset_index : 0;
    variable_item_set_current_value_index(item, index);
    variable_item_set_current_value_text(item, subghz_setting_get_preset_name(setting, index));
}

static void dual_build_items(void);

static void dual_add_jam_toggle(const char* label, uint32_t on, VariableItemChangeCallback cb) {
    VariableItem* item =
        variable_item_list_add(g_api->get_item_list(g_api->ctx), label, 2, cb, (void*)g_api);
    variable_item_set_current_value_index(item, on ? 1 : 0);
    variable_item_set_current_value_text(item, on ? "ON" : "OFF");
}

static void dual_rebuild_select(uint8_t sel) {
    VariableItemList* il = g_api->get_item_list(g_api->ctx);
    variable_item_list_reset(il);
    dual_build_items();
    variable_item_list_set_selected_item(il, sel);
}

static void dual_int_jamming_toggle(VariableItem* item) {
    const SubGhzConfigHostApi* api = variable_item_get_context(item);
    uint32_t on = variable_item_get_current_value_index(item) ? 1 : 0;
    api->get_last_settings(api->ctx)->dual_int_jamming = on;
    variable_item_set_current_value_text(item, on ? "ON" : "OFF");
    dual_rebuild_select(0);
}

static void dual_ext_jamming_toggle(VariableItem* item) {
    const SubGhzConfigHostApi* api = variable_item_get_context(item);
    SubGhzLastSettings* last = api->get_last_settings(api->ctx);
    uint32_t on = variable_item_get_current_value_index(item) ? 1 : 0;
    last->dual_ext_jamming = on;
    variable_item_set_current_value_text(item, on ? "ON" : "OFF");

    uint8_t ext_toggle_idx = (uint8_t)(1 + (last->dual_int_jamming ? 5 : 3));
    dual_rebuild_select(ext_toggle_idx);
}

#define DUAL_RSSI_COUNT 12
static const int16_t dual_rssi_dbm[DUAL_RSSI_COUNT] =
    {-100, -95, -90, -85, -80, -75, -70, -65, -60, -55, -50, -45};
static const char* const dual_onoff[2] = {"OFF", "ON"};

static void dual_rssi_text(VariableItem* item, uint32_t idx) {
    if(idx >= DUAL_RSSI_COUNT) idx = DUAL_RSSI_COUNT - 1;
    char buf[12];
    snprintf(buf, sizeof(buf), "%d dB", (int)dual_rssi_dbm[idx]);
    variable_item_set_current_value_text(item, buf);
}

static void dual_int_rssi_changed(VariableItem* item) {
    uint32_t idx = variable_item_get_current_value_index(item);
    g_api->get_last_settings(g_api->ctx)->dual_int_rssi = idx;
    dual_rssi_text(item, idx);
}

static void dual_ext_rssi_changed(VariableItem* item) {
    uint32_t idx = variable_item_get_current_value_index(item);
    g_api->get_last_settings(g_api->ctx)->dual_ext_rssi = idx;
    dual_rssi_text(item, idx);
}

static void dual_adaptive_changed(VariableItem* item) {
    uint32_t on = variable_item_get_current_value_index(item) ? 1 : 0;
    g_api->get_last_settings(g_api->ctx)->dual_rssi_adaptive = on;
    variable_item_set_current_value_text(item, dual_onoff[on]);
}

static void dual_unknown_raw_changed(VariableItem* item) {
    uint32_t on = variable_item_get_current_value_index(item) ? 1 : 0;
    g_api->get_last_settings(g_api->ctx)->capture_unknown_raw = on == 1;
    variable_item_set_current_value_text(item, dual_onoff[on]);
}

static void dual_psa_changed(VariableItem* item) {
    uint32_t on = variable_item_get_current_value_index(item) ? 1 : 0;
    g_api->get_last_settings(g_api->ctx)->show_psa_encrypted = on == 1;
    variable_item_set_current_value_text(item, dual_onoff[on]);
}

static void dual_add_rssi(const char* label, uint32_t idx, VariableItemChangeCallback cb) {
    VariableItem* item =
        variable_item_list_add(g_api->get_item_list(g_api->ctx), label, DUAL_RSSI_COUNT, cb, (void*)g_api);
    if(idx >= DUAL_RSSI_COUNT) idx = DUAL_RSSI_COUNT - 1;
    variable_item_set_current_value_index(item, (uint8_t)idx);
    dual_rssi_text(item, idx);
}

static void dual_add_onoff(const char* label, uint32_t on, VariableItemChangeCallback cb) {
    VariableItem* item =
        variable_item_list_add(g_api->get_item_list(g_api->ctx), label, 2, cb, (void*)g_api);
    variable_item_set_current_value_index(item, on ? 1 : 0);
    variable_item_set_current_value_text(item, dual_onoff[on ? 1 : 0]);
}

static void dual_build_items(void) {
    SubGhzLastSettings* last = g_api->get_last_settings(g_api->ctx);

    dual_add_jam_toggle("Int Jamming", last->dual_int_jamming, dual_int_jamming_toggle);
    if(last->dual_int_jamming) {
        dual_add_frequency("Int Jam Freq", last->dual_int_jam_frequency, dual_int_jam_frequency);
        dual_add_modulation("Int Jam Mod", last->dual_int_jam_preset_index, dual_int_jam_modulation);
    }
    dual_add_frequency("Int Frequency", last->dual_int_frequency, dual_int_frequency);
    dual_add_modulation("Int Modulation", last->dual_int_preset_index, dual_int_modulation);
    dual_add_rssi("Int RSSI", last->dual_int_rssi, dual_int_rssi_changed);

    dual_add_jam_toggle("Ext Jamming", last->dual_ext_jamming, dual_ext_jamming_toggle);
    if(last->dual_ext_jamming) {
        dual_add_frequency("Ext Jam Freq", last->dual_ext_jam_frequency, dual_ext_jam_frequency);
        dual_add_modulation("Ext Jam Mod", last->dual_ext_jam_preset_index, dual_ext_jam_modulation);
    }
    dual_add_frequency("Ext Frequency", last->dual_ext_frequency, dual_ext_frequency);
    dual_add_modulation("Ext Modulation", last->dual_ext_preset_index, dual_ext_modulation);
    dual_add_rssi("Ext RSSI", last->dual_ext_rssi, dual_ext_rssi_changed);

    dual_add_onoff("Adaptive RSSI", last->dual_rssi_adaptive, dual_adaptive_changed);
    dual_add_onoff("Unknown RAW", last->capture_unknown_raw ? 1 : 0, dual_unknown_raw_changed);
    dual_add_onoff("PSA (encrypted)", last->show_psa_encrypted ? 1 : 0, dual_psa_changed);
}

static void dual_plugin_set_host_api(const SubGhzConfigHostApi* api) {
    g_api = api;
}

static void dual_plugin_on_enter(void) {
    dual_build_items();
    g_api->switch_to_item_list_view(g_api->ctx);
}

static bool dual_plugin_on_event(SceneManagerEvent event) {
    UNUSED(event);
    return false;
}

static void dual_plugin_on_exit(void) {
    g_api->save_all(g_api->ctx);
    VariableItemList* il = g_api->get_item_list(g_api->ctx);
    variable_item_list_set_selected_item(il, 0);
    variable_item_list_reset(il);
}

static const SubGhzConfigScenePlugin dual_plugin = {
    .set_host_api = dual_plugin_set_host_api,
    .on_enter = dual_plugin_on_enter,
    .on_event = dual_plugin_on_event,
    .on_exit = dual_plugin_on_exit,
};

static const FlipperAppPluginDescriptor dual_plugin_descriptor = {
    .appid = SUBGHZ_CONFIG_SCENE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_CONFIG_SCENE_PLUGIN_API_VERSION,
    .entry_point = &dual_plugin,
};

const FlipperAppPluginDescriptor* subghz_config_dual_ep(void) {
    return &dual_plugin_descriptor;
}
