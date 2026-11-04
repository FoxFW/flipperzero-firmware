/* FoxFW2.0 config-externalization Stage 2 (2026-10-31).
 * Radio/ext-module settings scene as an on-demand .fal. Reaches SubGhz AND
 * furi_hal state only through SubGhzConfigHostApi; never includes subghz_i.h or
 * any furi_hal header. Faithful port of the former resident
 * subghz_scene_radio_settings.c. */

#include "../helpers/subghz_config_scene.h"
#include <lib/toolbox/value_index.h>
#include <stdio.h>

#define RADIO_SETTINGS_ROW_FILE_PREFIX 5
#define RADIO_SETTINGS_ROW_CUSTOM_FREQ 6

#define RADIO_DEVICE_COUNT 2
static const char* const radio_device_text[RADIO_DEVICE_COUNT] = {"Internal", "External"};

#define ON_OFF_COUNT 2
static const char* const on_off_text[ON_OFF_COUNT] = {"OFF", "ON"};

#define DEBUG_P_COUNT 2
static const char* const debug_pin_text[DEBUG_P_COUNT] = {"OFF", "17(1W)"};

#define DEBUG_COUNTER_COUNT 17
static const char* const debug_counter_text[DEBUG_COUNTER_COUNT] = {
    "+1", "+2", "+3", "+4", "+5", "+10", "+50", "OVFL", "OFEX",
    "No", "-1", "-2", "-3", "-4", "-5", "-10", "-50"};
static const int32_t debug_counter_val[DEBUG_COUNTER_COUNT] = {
    1, 2, 3, 4, 5, 10, 50, 65535, -2147483647, 0, -1, -2, -3, -4, -5, -10, -50};

#define TX_POWER_COUNT 9
static const char* const tx_power_text[TX_POWER_COUNT] = {
    "Preset", "10dBm +", "7dBm", "5dBm", "0dBm", "-10dBm", "-15dBm", "-20dBm", "-30dBm"};

#define FREQ_OFFSET_COUNT 101
#define FREQ_OFFSET_CENTER 50
#define FREQ_OFFSET_STEP 10000

#define VIZ_MODE_COUNT 2
static const char* const viz_mode_text[VIZ_MODE_COUNT] = {"Bar", "Line"};
static const uint32_t viz_mode_value[VIZ_MODE_COUNT] = {0, 1};

#define RADIO_RSSI_THRESHOLD_COUNT 11
static const char* const radio_rssi_threshold_text[RADIO_RSSI_THRESHOLD_COUNT] = {
    "-----", "-85.0", "-80.0", "-75.0", "-70.0", "-65.0",
    "-60.0", "-55.0", "-50.0", "-45.0", "-40.0"};
static const float radio_rssi_threshold_value[RADIO_RSSI_THRESHOLD_COUNT] = {
    -90.0f, -85.0f, -80.0f, -75.0f, -70.0f, -65.0f,
    -60.0f, -55.0f, -50.0f, -45.0f, -40.0f};

static const SubGhzConfigHostApi* g_api = NULL;

static void radio_set_freq_offset(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    int32_t offset_hz = ((int32_t)index - FREQ_OFFSET_CENTER) * FREQ_OFFSET_STEP;
    char offset_str[16];
    if(offset_hz == 0)
        snprintf(offset_str, sizeof(offset_str), "0 kHz");
    else
        snprintf(offset_str, sizeof(offset_str), "%+ld kHz", (long)(offset_hz / 1000));
    variable_item_set_current_value_text(item, offset_str);
    g_api->get_last_settings(g_api->ctx)->frequency_offset = offset_hz;
    g_api->set_freq_offset_hw(g_api->ctx, offset_hz);
    g_api->save_all(g_api->ctx);
}

static void radio_set_viz_mode(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, viz_mode_text[index]);
    g_api->get_last_settings(g_api->ctx)->visualizer_display_mode = viz_mode_value[index];
    g_api->save_all(g_api->ctx);
}

static void radio_set_device(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    uint8_t applied = g_api->device_set_index(g_api->ctx, index);
    variable_item_set_current_value_text(item, radio_device_text[applied]);
}

static void radio_set_tx_power(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, tx_power_text[index]);
    g_api->set_tx_power(g_api->ctx, index);
    g_api->save_all(g_api->ctx);
}

static void radio_set_debug_pin(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, debug_pin_text[index]);
    g_api->set_debug_pin_state(g_api->ctx, index == 1);
}

static void radio_set_ext_amp_leds(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, on_off_text[index]);
    SubGhzLastSettings* last = g_api->get_last_settings(g_api->ctx);
    last->leds_and_amp = index == 1;
    g_api->set_ext_leds_and_amp(g_api->ctx, last->leds_and_amp);
    g_api->save_all(g_api->ctx);
    g_api->reseat_radio_device(g_api->ctx);
}

static void radio_set_debug_counter(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, debug_counter_text[index]);
    g_api->set_rolling_counter_mult(g_api->ctx, debug_counter_val[index]);
}

static void radio_set_protocol_names(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, on_off_text[index]);
    g_api->get_last_settings(g_api->ctx)->protocol_file_names = (index == 1);
    g_api->save_all(g_api->ctx);
}

static void radio_set_bypass_region_lock(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, on_off_text[index]);
    SubGhzLastSettings* last = g_api->get_last_settings(g_api->ctx);
    last->bypass_region_lock = (index == 1);
    g_api->set_dangerous_frequency(g_api->ctx, last->bypass_region_lock);
    g_api->save_all(g_api->ctx);
}

static void radio_set_rssi_threshold(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, radio_rssi_threshold_text[index]);
    g_api->threshold_rssi_set(g_api->ctx, radio_rssi_threshold_value[index]);
    g_api->get_last_settings(g_api->ctx)->rssi = radio_rssi_threshold_value[index];
    g_api->save_all(g_api->ctx);
}

static void radio_enter_cb(void* context, uint32_t index) {
    UNUSED(context);
    view_dispatcher_send_custom_event(g_api->get_view_dispatcher(g_api->ctx), index);
}

static void radio_plugin_set_host_api(const SubGhzConfigHostApi* api) {
    g_api = api;
}

static void radio_plugin_on_enter(void) {
    VariableItemList* list = g_api->get_item_list(g_api->ctx);
    SubGhzLastSettings* last = g_api->get_last_settings(g_api->ctx);
    int32_t value_index;
    VariableItem* item;

    item = variable_item_list_add(list, "Visualizer Graph Style", VIZ_MODE_COUNT, radio_set_viz_mode, NULL);
    value_index = value_index_uint32(last->visualizer_display_mode, viz_mode_value, VIZ_MODE_COUNT);
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, viz_mode_text[value_index]);

    uint8_t value_count_device = RADIO_DEVICE_COUNT;
    if(g_api->device_get_index(g_api->ctx) == 0 && !g_api->device_external_available(g_api->ctx))
        value_count_device = 1;
    item = variable_item_list_add(list, "Module", value_count_device, radio_set_device, NULL);
    value_index = g_api->device_get_index(g_api->ctx);
    if(value_index >= value_count_device) value_index = 0;
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, radio_device_text[value_index]);

    item = variable_item_list_add(list, "TX Power", TX_POWER_COUNT, radio_set_tx_power, NULL);
    value_index = g_api->get_tx_power(g_api->ctx);
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, tx_power_text[value_index]);

    item = variable_item_list_add(list, "Protocol Names", ON_OFF_COUNT, radio_set_protocol_names, NULL);
    value_index = last->protocol_file_names;
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, on_off_text[value_index]);

    item = variable_item_list_add(list, "Bypass Region Lock", ON_OFF_COUNT, radio_set_bypass_region_lock, NULL);
    value_index = last->bypass_region_lock ? 1 : 0;
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, on_off_text[value_index]);

    item = variable_item_list_add(list, "File Prefix", 0, NULL, NULL);
    if(last->file_prefix[0] != '\0')
        variable_item_set_current_value_text(item, last->file_prefix);
    else
        variable_item_set_current_value_text(item, "(none)");

    variable_item_list_add(list, "Custom Frequencies", 0, NULL, NULL);

    item = variable_item_list_add(list, "Freq Offset", FREQ_OFFSET_COUNT, radio_set_freq_offset, NULL);
    {
        int32_t offset_hz = last->frequency_offset;
        int32_t offset_idx_signed = (offset_hz / FREQ_OFFSET_STEP) + FREQ_OFFSET_CENTER;
        if(offset_idx_signed < 0) offset_idx_signed = 0;
        if(offset_idx_signed > FREQ_OFFSET_COUNT - 1) offset_idx_signed = FREQ_OFFSET_COUNT - 1;
        uint8_t offset_idx = (uint8_t)offset_idx_signed;
        variable_item_set_current_value_index(item, offset_idx);
        char offset_str[16];
        if(offset_hz == 0)
            snprintf(offset_str, sizeof(offset_str), "0 kHz");
        else
            snprintf(offset_str, sizeof(offset_str), "%+ld kHz", (long)(offset_hz / 1000));
        variable_item_set_current_value_text(item, offset_str);
    }

    bool debug = g_api->is_debug_mode(g_api->ctx);
    item = variable_item_list_add(
        list, "Counter Incr.", debug ? DEBUG_COUNTER_COUNT : 3, radio_set_debug_counter, NULL);
    value_index = value_index_int32(
        g_api->get_rolling_counter_mult(g_api->ctx), debug_counter_val, debug ? DEBUG_COUNTER_COUNT : 3);
    g_api->set_rolling_counter_mult(g_api->ctx, debug_counter_val[value_index]);
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, debug_counter_text[value_index]);

    if(debug) {
        item = variable_item_list_add(list, "Ext Amp & LEDs", ON_OFF_COUNT, radio_set_ext_amp_leds, NULL);
        value_index = last->leds_and_amp ? 1 : 0;
        variable_item_set_current_value_index(item, value_index);
        variable_item_set_current_value_text(item, on_off_text[value_index]);

        item = variable_item_list_add(list, "Debug Pin", DEBUG_P_COUNT, radio_set_debug_pin, NULL);
        value_index = g_api->get_debug_pin_state(g_api->ctx);
        variable_item_set_current_value_index(item, value_index);
        variable_item_set_current_value_text(item, debug_pin_text[value_index]);
    }

    item = variable_item_list_add(
        list, "RSSI Threshold:", RADIO_RSSI_THRESHOLD_COUNT, radio_set_rssi_threshold, NULL);
    value_index = value_index_float(
        g_api->threshold_rssi_get(g_api->ctx), radio_rssi_threshold_value, RADIO_RSSI_THRESHOLD_COUNT);
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, radio_rssi_threshold_text[value_index]);

    variable_item_list_set_enter_callback(list, radio_enter_cb, (void*)g_api);
    g_api->switch_to_item_list_view(g_api->ctx);
}

static bool radio_plugin_on_event(SceneManagerEvent event) {
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == RADIO_SETTINGS_ROW_FILE_PREFIX) {
            g_api->open_scene(g_api->ctx, SubGhzConfigNavFilePrefix);
            return true;
        } else if(event.event == RADIO_SETTINGS_ROW_CUSTOM_FREQ) {
            g_api->open_scene(g_api->ctx, SubGhzConfigNavCustomFreq);
            return true;
        }
    }
    return false;
}

static void radio_plugin_on_exit(void) {
    VariableItemList* il = g_api->get_item_list(g_api->ctx);
    variable_item_list_set_selected_item(il, 0);
    variable_item_list_reset(il);
}

static const SubGhzConfigScenePlugin radio_plugin = {
    .set_host_api = radio_plugin_set_host_api,
    .on_enter = radio_plugin_on_enter,
    .on_event = radio_plugin_on_event,
    .on_exit = radio_plugin_on_exit,
};

static const FlipperAppPluginDescriptor radio_plugin_descriptor = {
    .appid = SUBGHZ_CONFIG_SCENE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_CONFIG_SCENE_PLUGIN_API_VERSION,
    .entry_point = &radio_plugin,
};

const FlipperAppPluginDescriptor* subghz_config_radio_ep(void) {
    return &radio_plugin_descriptor;
}
