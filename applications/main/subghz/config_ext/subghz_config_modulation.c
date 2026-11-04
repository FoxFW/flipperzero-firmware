/* FoxFW2.0 config-externalization Stage 2 (2026-10-31, fixed 2026-10-31).
 * Modulation-filter list config scene as an on-demand .fal. Reaches SubGhz state
 * only through SubGhzConfigHostApi; never includes subghz_i.h.
 *
 * NOTE: VariableItemList stores items in an M-lib ARRAY (by value); adding past
 * capacity reallocates and MOVES items, so a cached VariableItem* dangles. We
 * therefore reserve capacity up front (keeps the count item valid for the tick
 * handler) and re-fetch each modulation row via variable_item_list_get() in the
 * select-all/none path instead of caching pointers -- the same safe pattern the
 * resident Protocol List scene uses. Select is applied in the change callback so
 * it is automatic (matches Protocol List). */

#include "../helpers/subghz_config_scene.h"
#include "../subghz_modulation_filter.h" /* for SUBGHZ_MOD_FILTER_COUNT only */
#include <string.h>
#include <stdio.h>

static const SubGhzConfigHostApi* g_api = NULL;

static const char* const mod_toggle_labels[] = {"OFF", "ON"};
static const char* const mod_select_labels[] = {"All", "None"};
#define MOD_SELECT_ROW 1u
#define MOD_FIRST_ROW  2u /* row 0 = count, row 1 = Select, rows 2+ = modulations */

static VariableItem* g_mod_count_item = NULL;
static bool g_mod_count_dirty = false;
static uint8_t g_mod_select_choice = 0;

static void build_mod_count_text(char* buf, size_t buf_size) {
    SubGhzSetting* setting = g_api->get_setting(g_api->ctx);
    size_t total = subghz_setting_get_preset_count(setting);
    size_t enabled = 0;
    for(size_t i = 0; i < total; i++) {
        if(g_api->mod_filter_is_enabled(g_api->ctx, i)) enabled++;
    }
    snprintf(buf, buf_size, "%u/%u", (unsigned)enabled, (unsigned)total);
}

static void mod_toggle_cb(VariableItem* item) {
    uintptr_t idx = (uintptr_t)variable_item_get_context(item);
    SubGhzSetting* setting = g_api->get_setting(g_api->ctx);
    if(idx >= subghz_setting_get_preset_count(setting)) return;

    uint8_t val = variable_item_get_current_value_index(item);

    if(val == 0) {
        size_t tot2 = subghz_setting_get_preset_count(setting);
        size_t en_count = 0;
        for(size_t j = 0; j < tot2; j++)
            if(g_api->mod_filter_is_enabled(g_api->ctx, j)) en_count++;
        if(en_count <= 1 && g_api->mod_filter_is_enabled(g_api->ctx, idx)) {
            variable_item_set_current_value_index(item, 1);
            variable_item_set_current_value_text(item, mod_toggle_labels[1]);
            return;
        }
    }
    variable_item_set_current_value_text(item, mod_toggle_labels[val]);
    g_api->mod_filter_set_enabled(g_api->ctx, idx, (bool)val);
    g_mod_count_dirty = true;
}

/* Apply All/None by RE-FETCHING each row (never cache VariableItem* across adds). */
static void mod_select_apply(bool enable_all) {
    VariableItemList* list = g_api->get_item_list(g_api->ctx);
    SubGhzSetting* setting = g_api->get_setting(g_api->ctx);
    size_t total = subghz_setting_get_preset_count(setting);
    for(size_t i = 0; i < total && i < SUBGHZ_MOD_FILTER_COUNT; i++) {
        g_api->mod_filter_set_enabled(g_api->ctx, i, enable_all);
        size_t row = MOD_FIRST_ROW + i;
        if(row > UINT8_MAX) break;
        VariableItem* it = variable_item_list_get(list, (uint8_t)row);
        if(!it) continue;
        variable_item_set_current_value_index(it, enable_all ? 1 : 0);
        variable_item_set_current_value_text(it, mod_toggle_labels[enable_all ? 1 : 0]);
    }
    g_mod_count_dirty = true;
}

static void mod_select_value_cb(VariableItem* item) {
    g_mod_select_choice = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, mod_select_labels[g_mod_select_choice]);
    mod_select_apply(g_mod_select_choice == 0); /* automatic, like Protocol List */
}

static void mod_list_enter_cb(void* context, uint32_t index) {
    UNUSED(context);
    if(index != MOD_SELECT_ROW) return;
    mod_select_apply(g_mod_select_choice == 0);
}

static void mod_list_populate(void) {
    VariableItemList* list = g_api->get_item_list(g_api->ctx);
    SubGhzSetting* setting = g_api->get_setting(g_api->ctx);
    size_t total = subghz_setting_get_preset_count(setting);

    variable_item_list_reset(list);
    variable_item_list_reserve(list, total + 2); /* no realloc during adds -> pointers stable */

    char count_buf[16];
    build_mod_count_text(count_buf, sizeof(count_buf));
    g_mod_count_item = variable_item_list_add(list, "Modulations", 1, NULL, NULL);
    variable_item_set_current_value_index(g_mod_count_item, 0);
    variable_item_set_current_value_text(g_mod_count_item, count_buf);
    g_mod_count_dirty = false;

    VariableItem* sel = variable_item_list_add(list, "Select", 2, mod_select_value_cb, NULL);
    variable_item_set_current_value_index(sel, g_mod_select_choice);
    variable_item_set_current_value_text(sel, mod_select_labels[g_mod_select_choice]);

    for(size_t i = 0; i < total && i < SUBGHZ_MOD_FILTER_COUNT; i++) {
        const char* name = subghz_setting_get_preset_name(setting, i);
        bool enabled = g_api->mod_filter_is_enabled(g_api->ctx, i);
        VariableItem* item =
            variable_item_list_add(list, name ? name : "?", 2, mod_toggle_cb, (void*)(uintptr_t)i);
        variable_item_set_current_value_index(item, enabled ? 1 : 0);
        variable_item_set_current_value_text(item, mod_toggle_labels[enabled ? 1 : 0]);
    }
}

static void mod_plugin_set_host_api(const SubGhzConfigHostApi* api) {
    g_api = api;
}

static void mod_plugin_on_enter(void) {
    g_mod_select_choice = 0;
    mod_list_populate();
    variable_item_list_set_enter_callback(
        g_api->get_item_list(g_api->ctx), mod_list_enter_cb, (void*)g_api);
    g_api->switch_to_item_list_view(g_api->ctx);
}

static bool mod_plugin_on_event(SceneManagerEvent event) {
    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom) {
        g_api->set_scene_state(g_api->ctx, event.event);
        consumed = true;
    } else if(event.type == SceneManagerEventTypeBack) {
        g_api->save_all(g_api->ctx);
        consumed = false;
    } else if(event.type == SceneManagerEventTypeTick) {
        if(g_mod_count_dirty && g_mod_count_item) {
            char count_buf[16];
            build_mod_count_text(count_buf, sizeof(count_buf));
            variable_item_set_current_value_text(g_mod_count_item, count_buf);
            g_mod_count_dirty = false;
        }
        consumed = true;
    }
    return consumed;
}

static void mod_plugin_on_exit(void) {
    g_api->save_all(g_api->ctx);
    variable_item_list_reset(g_api->get_item_list(g_api->ctx));
    g_mod_count_item = NULL;
    g_mod_count_dirty = false;
}

static const SubGhzConfigScenePlugin mod_plugin = {
    .set_host_api = mod_plugin_set_host_api,
    .on_enter = mod_plugin_on_enter,
    .on_event = mod_plugin_on_event,
    .on_exit = mod_plugin_on_exit,
};

static const FlipperAppPluginDescriptor mod_plugin_descriptor = {
    .appid = SUBGHZ_CONFIG_SCENE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_CONFIG_SCENE_PLUGIN_API_VERSION,
    .entry_point = &mod_plugin,
};

const FlipperAppPluginDescriptor* subghz_config_modulation_ep(void) {
    return &mod_plugin_descriptor;
}
