#include "../keeloq_tool_i.h"

#define KEELOQ_KEY_LIST_ADD   0u
#define KEELOQ_KEY_OPT_EDIT   0xFFFFFF01u
#define KEELOQ_KEY_OPT_DELETE 0xFFFFFF02u

static bool kl_opts_mode = false;
static bool kl_info_mode = false;
static char kl_info_text[48];

static bool kl_going_to_edit = false;

static void keeloq_keys_submenu_callback(void* context, uint32_t index) {
    KeeloqTool* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void keeloq_keys_show_list(KeeloqTool* app) {
    submenu_reset(app->submenu);
    size_t n_user = subghz_keeloq_keys_user_count(app->keeloq_keys_manager);
    size_t n_total = subghz_keeloq_keys_count(app->keeloq_keys_manager);
    size_t n_sys = n_total - n_user;
    char header[32];
    snprintf(header, sizeof(header), "KL Keys U:%zu S:%zu", n_user, n_sys);
    submenu_set_header(app->submenu, header);
    submenu_add_item(
        app->submenu, "+ Add Key", KEELOQ_KEY_LIST_ADD, keeloq_keys_submenu_callback, app);
    for(size_t i = 0; i < n_total; i++) {
        SubGhzKey* k = subghz_keeloq_keys_get(app->keeloq_keys_manager, i);
        submenu_add_item(
            app->submenu,
            furi_string_get_cstr(k->name),
            (uint32_t)(i + 1),
            keeloq_keys_submenu_callback,
            app);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, KeeloqToolViewSubmenu);
}

static void keeloq_keys_show_options(KeeloqTool* app) {
    SubGhzKey* k =
        subghz_keeloq_keys_get(app->keeloq_keys_manager, app->keeloq_edit.edit_index);
    if(!k) return;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, furi_string_get_cstr(k->name));
    submenu_add_item(
        app->submenu, "Edit", KEELOQ_KEY_OPT_EDIT, keeloq_keys_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Delete", KEELOQ_KEY_OPT_DELETE, keeloq_keys_submenu_callback, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, KeeloqToolViewSubmenu);
}

static void keeloq_keys_show_info(KeeloqTool* app, size_t idx) {
    SubGhzKey* k = subghz_keeloq_keys_get(app->keeloq_keys_manager, idx);
    if(!k) return;
    popup_reset(app->popup);
    popup_set_header(
        app->popup, furi_string_get_cstr(k->name), 64, 5, AlignCenter, AlignTop);
    snprintf(
        kl_info_text,
        sizeof(kl_info_text),
        "%016llX\nType: %hu",
        (unsigned long long)k->key,
        k->type);
    popup_set_text(app->popup, kl_info_text, 64, 28, AlignCenter, AlignTop);
    popup_disable_timeout(app->popup);
    view_dispatcher_switch_to_view(app->view_dispatcher, KeeloqToolViewPopup);
    kl_info_mode = true;
}

void keeloq_tool_scene_keys_on_enter(void* context) {
    KeeloqTool* app = context;
    kl_opts_mode = false;
    kl_info_mode = false;
    kl_going_to_edit = false;
    if(!app->keeloq_keys_manager) {
        app->keeloq_keys_manager = subghz_keeloq_keys_alloc();
    }
    keeloq_keys_show_list(app);
}

bool keeloq_tool_scene_keys_on_event(void* context, SceneManagerEvent event) {
    KeeloqTool* app = context;

    if(event.type == SceneManagerEventTypeBack) {
        if(kl_info_mode) {
            kl_info_mode = false;
            keeloq_keys_show_list(app);
            return true;
        }
        if(kl_opts_mode) {
            kl_opts_mode = false;
            keeloq_keys_show_list(app);
            return true;
        }
        return false;
    }

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == KEELOQ_KEY_LIST_ADD) {
            app->keeloq_edit.is_new = true;
            app->keeloq_edit.edit_step = 0;
            memset(app->keeloq_edit.key_bytes, 0, sizeof(app->keeloq_edit.key_bytes));
            memset(app->keeloq_edit.name, 0, sizeof(app->keeloq_edit.name));
            app->keeloq_edit.type = 1;
            kl_going_to_edit = true;
            scene_manager_next_scene(app->scene_manager, KeeloqToolSceneKeyEdit);
            return true;
        } else if(event.event >= 1 && event.event < KEELOQ_KEY_OPT_EDIT) {
            size_t idx = (size_t)(event.event - 1);
            size_t n_user = subghz_keeloq_keys_user_count(app->keeloq_keys_manager);
            if(idx >= n_user) {

                keeloq_keys_show_info(app, idx);
                return true;
            }
            app->keeloq_edit.edit_index = idx;
            app->keeloq_edit.is_new = false;
            kl_opts_mode = true;
            keeloq_keys_show_options(app);
            return true;
        } else if(event.event == KEELOQ_KEY_OPT_EDIT) {
            kl_opts_mode = false;
            app->keeloq_edit.edit_step = 0;
            SubGhzKey* k = subghz_keeloq_keys_get(
                app->keeloq_keys_manager, app->keeloq_edit.edit_index);
            if(!k) return true;
            uint64_t kv = k->key;
            for(int b = 7; b >= 0; b--) {
                app->keeloq_edit.key_bytes[b] = (uint8_t)(kv & 0xFF);
                kv >>= 8;
            }
            strncpy(
                app->keeloq_edit.name,
                furi_string_get_cstr(k->name),
                sizeof(app->keeloq_edit.name) - 1);
            app->keeloq_edit.name[sizeof(app->keeloq_edit.name) - 1] = '\0';
            app->keeloq_edit.type = k->type;
            kl_going_to_edit = true;
            scene_manager_next_scene(app->scene_manager, KeeloqToolSceneKeyEdit);
            return true;
        } else if(event.event == KEELOQ_KEY_OPT_DELETE) {
            kl_opts_mode = false;
            subghz_keeloq_keys_delete(
                app->keeloq_keys_manager, app->keeloq_edit.edit_index);
            subghz_keeloq_keys_save(app->keeloq_keys_manager);
            keeloq_keys_show_list(app);
            return true;
        }
    }
    return false;
}

void keeloq_tool_scene_keys_on_exit(void* context) {
    KeeloqTool* app = context;
    kl_opts_mode = false;
    kl_info_mode = false;
    submenu_reset(app->submenu);
    if(!kl_going_to_edit) {
        if(app->keeloq_keys_manager) {
            subghz_keeloq_keys_free(app->keeloq_keys_manager);
            app->keeloq_keys_manager = NULL;
        }
    }
    kl_going_to_edit = false;
}
