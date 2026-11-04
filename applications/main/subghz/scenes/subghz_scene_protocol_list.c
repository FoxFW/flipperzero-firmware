#include "../subghz_i.h"
#include "../subghz_protocol_filter.h"
#include <lib/subghz/subghz_protocol_registry.h>
#include <string.h>

#define PROTO_LIST_MAX   256u
#define SELECT_ROW_INDEX 1u

static const char* const proto_toggle_labels[] = {"OFF", "ON"};
static const char* const select_labels[] = {"All", "None"};

typedef struct {
    uint16_t protocol_index;
    bool enabled;
} ProtoItemCtx;

static ProtoItemCtx* g_proto_ctx = NULL;
static size_t g_proto_ctx_count = 0;
static uint8_t g_select_choice = 0;
static VariableItem* g_count_item = NULL;
static bool g_filter_dirty = false;
static bool g_count_dirty = false;
static SubGhz* g_proto_subghz = NULL;
static uint8_t g_first_proto_row = 0;

static void build_count_text(SubGhz* subghz, size_t total, char* buf, size_t n) {
    size_t active = subghz_protocol_filter_enabled_count(subghz->protocol_filter, total);
    if(active == total)
        snprintf(buf, n, "%zu", total);
    else
        snprintf(buf, n, "%zu/%zu", active, total);
}

static void apply_items_to_filter(void) {
    if(!g_proto_subghz || !g_proto_subghz->protocol_filter) return;
    for(size_t i = 0; i < g_proto_ctx_count; i++) {
        subghz_protocol_filter_set_enabled(
            g_proto_subghz->protocol_filter, g_proto_ctx[i].protocol_index, g_proto_ctx[i].enabled);
    }
    g_filter_dirty = false;
}

static void proto_toggle_cb(VariableItem* item) {
    uintptr_t ctx_raw = (uintptr_t)variable_item_get_context(item);
    if(!ctx_raw) return;
    size_t ci = ctx_raw - 1;
    if(!g_proto_ctx || ci >= g_proto_ctx_count) return;

    uint8_t val = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, proto_toggle_labels[val]);
    g_proto_ctx[ci].enabled = (val != 0);
    g_filter_dirty = true;
    g_count_dirty = true;
}

static void select_apply(bool enable) {
    if(!g_proto_subghz) return;
    VariableItemList* list = g_proto_subghz->variable_item_list;
    for(size_t ci = 0; ci < g_proto_ctx_count; ci++) {
        g_proto_ctx[ci].enabled = enable;
        size_t row = g_first_proto_row + ci;
        if(row > UINT8_MAX) break;
        VariableItem* it = variable_item_list_get(list, (uint8_t)row);
        if(!it) continue;
        variable_item_set_current_value_index(it, enable ? 1 : 0);
        variable_item_set_current_value_text(it, proto_toggle_labels[enable ? 1 : 0]);
    }
    apply_items_to_filter();
    g_count_dirty = true;
}

static void select_value_change_cb(VariableItem* item) {
    g_select_choice = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, select_labels[g_select_choice]);
    select_apply(g_select_choice == 0);
}

static bool is_raw_family(const char* name) {
    return strcmp(name, "RAW") == 0 || strcmp(name, "BinRAW") == 0;
}

static int name_cmp_nocase(const char* a, const char* b) {
    for(;; a++, b++) {
        char ca = *a, cb = *b;
        if(ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if(cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if(ca != cb || ca == '\0') return (unsigned char)ca - (unsigned char)cb;
    }
}

static const char* proto_name(size_t index) {
    const SubGhzProtocol* proto =
        subghz_protocol_registry_get_by_index(&subghz_protocol_registry, index);
    return proto ? proto->name : NULL;
}

static void protocol_list_populate(SubGhz* subghz) {
    VariableItemList* list = subghz->variable_item_list;
    variable_item_list_reset(list);

    if(g_proto_ctx) {
        free(g_proto_ctx);
        g_proto_ctx = NULL;
    }
    g_proto_ctx_count = 0;

    size_t total = subghz_protocol_registry_count(&subghz_protocol_registry);
    if(total > PROTO_LIST_MAX) total = PROTO_LIST_MAX;
    g_proto_ctx = malloc(sizeof(ProtoItemCtx) * total);
    furi_assert(g_proto_ctx);

    for(size_t i = 0; i < total; i++) {
        const char* name = proto_name(i);
        if(!name || is_raw_family(name)) continue;
        size_t pos = g_proto_ctx_count++;
        while(pos > 0 && name_cmp_nocase(proto_name(g_proto_ctx[pos - 1].protocol_index), name) > 0) {
            g_proto_ctx[pos] = g_proto_ctx[pos - 1];
            pos--;
        }
        g_proto_ctx[pos].protocol_index = (uint16_t)i;
        g_proto_ctx[pos].enabled = subghz_protocol_filter_is_enabled(subghz->protocol_filter, i);
    }

    variable_item_list_reserve(list, total + 2);

    char count_buf[16];
    build_count_text(subghz, total, count_buf, sizeof(count_buf));
    g_count_item = variable_item_list_add(list, "Protocols", 1, NULL, NULL);
    variable_item_set_current_value_index(g_count_item, 0);
    variable_item_set_current_value_text(g_count_item, count_buf);
    g_count_dirty = false;
    g_filter_dirty = false;

    VariableItem* sel = variable_item_list_add(list, "Select", 2, select_value_change_cb, NULL);
    variable_item_set_current_value_index(sel, g_select_choice);
    variable_item_set_current_value_text(sel, select_labels[g_select_choice]);

    g_first_proto_row = 2;
    for(size_t i = 0; i < total; i++) {
        const char* name = proto_name(i);
        if(name && is_raw_family(name)) {
            g_first_proto_row++;
            subghz_protocol_filter_set_enabled(subghz->protocol_filter, i, true);
            VariableItem* raw = variable_item_list_add(list, name, 1, NULL, NULL);
            variable_item_set_current_value_index(raw, 0);
            variable_item_set_current_value_text(raw, "ON");
        }
    }

    for(size_t ci = 0; ci < g_proto_ctx_count; ci++) {
        bool enabled = g_proto_ctx[ci].enabled;
        VariableItem* it = variable_item_list_add(
            list,
            proto_name(g_proto_ctx[ci].protocol_index),
            2,
            proto_toggle_cb,
            (void*)(uintptr_t)(ci + 1));
        variable_item_set_current_value_index(it, enabled ? 1 : 0);
        variable_item_set_current_value_text(it, proto_toggle_labels[enabled ? 1 : 0]);
    }
}

static void protocol_list_enter_cb(void* context, uint32_t index) {
    UNUSED(context);
    if(index != SELECT_ROW_INDEX) return;
    select_apply(g_select_choice == 0);
}

void subghz_scene_protocol_list_on_enter(void* context) {
    SubGhz* subghz = context;
    subghz_txrx_receiver_release(subghz->txrx);
    g_proto_subghz = subghz;
    g_select_choice = 0;

    protocol_list_populate(subghz);
    variable_item_list_set_enter_callback(
        subghz->variable_item_list, protocol_list_enter_cb, subghz);

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdVariableItemList);
}

bool subghz_scene_protocol_list_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(subghz->scene_manager, SubGhzSceneProtocolList, event.event);
        consumed = true;
    } else if(event.type == SceneManagerEventTypeBack) {
        if(g_filter_dirty) apply_items_to_filter();
        subghz_protocol_filter_save(subghz->protocol_filter);
        consumed = false;
    } else if(event.type == SceneManagerEventTypeTick) {
        if(g_filter_dirty) apply_items_to_filter();
        if(g_count_dirty && g_count_item && g_proto_subghz) {
            size_t total = subghz_protocol_registry_count(&subghz_protocol_registry);
            char count_buf[16];
            build_count_text(g_proto_subghz, total, count_buf, sizeof(count_buf));
            variable_item_set_current_value_text(g_count_item, count_buf);
            g_count_dirty = false;
        }
        consumed = true;
    }
    return consumed;
}

void subghz_scene_protocol_list_on_exit(void* context) {
    SubGhz* subghz = context;
    if(g_filter_dirty) apply_items_to_filter();
    subghz_protocol_filter_save(subghz->protocol_filter);
    variable_item_list_reset(subghz->variable_item_list);
    if(g_proto_ctx) {
        free(g_proto_ctx);
        g_proto_ctx = NULL;
    }
    g_proto_ctx_count = 0;
    g_count_item = NULL;
    g_filter_dirty = false;
    g_count_dirty = false;
    g_proto_subghz = NULL;
}
