/* FoxFW2.0 config-externalization Stage 1 PoC (2026-10-31).
 * Dual-RX config scene as an on-demand .fal. Reaches SubGhz state ONLY through
 * SubGhzConfigHostApi; never includes subghz_i.h. The only firmware struct it
 * touches directly is the stable SubGhzLastSettings leaf (dual_* fields).
 * Byte-for-byte behaviour port of the former resident scene. */

#include "../helpers/subghz_config_scene.h"
#include <stdio.h>

enum {
    DualIntFrequency,
    DualIntModulation,
    DualExtFrequency,
    DualExtModulation,
};

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

static void dual_plugin_set_host_api(const SubGhzConfigHostApi* api) {
    g_api = api;
}

static void dual_plugin_on_enter(void) {
    SubGhzLastSettings* last = g_api->get_last_settings(g_api->ctx);
    dual_add_frequency("Int Frequency", last->dual_int_frequency, dual_int_frequency);
    dual_add_modulation("Int Modulation", last->dual_int_preset_index, dual_int_modulation);
    dual_add_frequency("Ext Frequency", last->dual_ext_frequency, dual_ext_frequency);
    dual_add_modulation("Ext Modulation", last->dual_ext_preset_index, dual_ext_modulation);
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
