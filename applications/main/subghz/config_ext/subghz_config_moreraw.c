

#include "../helpers/subghz_config_scene.h"

static const SubGhzConfigHostApi* g_api = NULL;

enum SubmenuIndex {
    SubmenuIndexDecode,
    SubmenuIndexRawEdit,
    SubmenuIndexEdit,
    SubmenuIndexDelete,
};

static void more_raw_submenu_cb(void* context, uint32_t index) {
    UNUSED(context);
    view_dispatcher_send_custom_event(g_api->get_view_dispatcher(g_api->ctx), index);
}

static void more_raw_set_host_api(const SubGhzConfigHostApi* api) {
    g_api = api;
}

static void more_raw_on_enter(void) {
    Submenu* submenu = g_api->get_submenu(g_api->ctx);
    submenu_reset(submenu);

    submenu_add_item(submenu, "Decode", SubmenuIndexDecode, more_raw_submenu_cb, NULL);

    if(g_api->raw_edit_available(g_api->ctx)) {
        submenu_add_item(submenu, "Edit RAW", SubmenuIndexRawEdit, more_raw_submenu_cb, NULL);
    }

    submenu_add_item(submenu, "Rename", SubmenuIndexEdit, more_raw_submenu_cb, NULL);
    submenu_add_item(submenu, "Delete", SubmenuIndexDelete, more_raw_submenu_cb, NULL);

    submenu_set_selected_item(submenu, g_api->get_scene_state(g_api->ctx));
    g_api->switch_to_view(g_api->ctx, SubGhzConfigViewSubmenu);
}

static bool more_raw_on_event(SceneManagerEvent event) {
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubmenuIndexRawEdit) {
            if(g_api->file_available(g_api->ctx)) {
                g_api->launch_raw_edit(g_api->ctx);
                return true;
            } else {
                g_api->nav(g_api->ctx, SubGhzConfigNavSearchPrevOrStop, SubGhzConfigSceneStart);
            }
        } else if(event.event == SubmenuIndexDelete) {
            if(g_api->file_available(g_api->ctx)) {
                g_api->clear_read_raw_state(g_api->ctx);
                g_api->set_scene_state(g_api->ctx, SubmenuIndexDelete);
                g_api->nav(g_api->ctx, SubGhzConfigNavNext, SubGhzConfigSceneDeleteRAW);
                return true;
            } else {
                g_api->nav(g_api->ctx, SubGhzConfigNavSearchPrevOrStop, SubGhzConfigSceneStart);
            }
        } else if(event.event == SubmenuIndexEdit) {
            if(g_api->file_available(g_api->ctx)) {
                furi_string_reset(g_api->get_file_path_tmp(g_api->ctx));
                g_api->set_scene_state(g_api->ctx, SubmenuIndexEdit);
                g_api->nav(g_api->ctx, SubGhzConfigNavNext, SubGhzConfigSceneSaveName);
                return true;
            } else {
                g_api->nav(g_api->ctx, SubGhzConfigNavSearchPrevOrStop, SubGhzConfigSceneStart);
            }
        } else if(event.event == SubmenuIndexDecode) {
            if(g_api->file_available(g_api->ctx)) {
                g_api->set_scene_state(g_api->ctx, SubmenuIndexDecode);
                g_api->nav(g_api->ctx, SubGhzConfigNavNext, SubGhzConfigSceneDecodeRAW);
                return true;
            } else {
                g_api->nav(g_api->ctx, SubGhzConfigNavSearchPrevOrStop, SubGhzConfigSceneStart);
            }
        }
    }
    return false;
}

static void more_raw_on_exit(void) {
    submenu_reset(g_api->get_submenu(g_api->ctx));
}

static const SubGhzConfigScenePlugin more_raw_plugin = {
    .set_host_api = more_raw_set_host_api,
    .on_enter = more_raw_on_enter,
    .on_event = more_raw_on_event,
    .on_exit = more_raw_on_exit,
};

static const FlipperAppPluginDescriptor more_raw_plugin_descriptor = {
    .appid = SUBGHZ_CONFIG_SCENE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_CONFIG_SCENE_PLUGIN_API_VERSION,
    .entry_point = &more_raw_plugin,
};

const FlipperAppPluginDescriptor* subghz_config_moreraw_ep(void) {
    return &more_raw_plugin_descriptor;
}
