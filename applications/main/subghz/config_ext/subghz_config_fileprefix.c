

#include "../helpers/subghz_config_scene.h"
#include "../helpers/subghz_custom_event.h"
#include <string.h>

static const SubGhzConfigHostApi* g_api = NULL;

static void fileprefix_text_input_callback(void* context) {
    UNUSED(context);
    view_dispatcher_send_custom_event(
        g_api->get_view_dispatcher(g_api->ctx), SubGhzCustomEventSceneFilePrefixDone);
}

static void fileprefix_set_host_api(const SubGhzConfigHostApi* api) {
    g_api = api;
}

static void fileprefix_on_enter(void) {
    TextInput* text_input = g_api->get_text_input(g_api->ctx);
    SubGhzLastSettings* last = g_api->get_last_settings(g_api->ctx);
    char* name_tmp = g_api->get_file_name_tmp(g_api->ctx);

    strncpy(name_tmp, last->file_prefix, sizeof(last->file_prefix) - 1);
    name_tmp[sizeof(last->file_prefix) - 1] = '\0';

    text_input_set_header_text(text_input, "File Naming Prefix");
    text_input_set_result_callback(
        text_input,
        fileprefix_text_input_callback,
        NULL,
        name_tmp,
        sizeof(last->file_prefix),
        true);

    g_api->switch_to_view(g_api->ctx, SubGhzConfigViewTextInput);
}

static bool fileprefix_on_event(SceneManagerEvent event) {
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubGhzCustomEventSceneFilePrefixDone) {
            SubGhzLastSettings* last = g_api->get_last_settings(g_api->ctx);
            char* name_tmp = g_api->get_file_name_tmp(g_api->ctx);
            strncpy(last->file_prefix, name_tmp, sizeof(last->file_prefix) - 1);
            last->file_prefix[sizeof(last->file_prefix) - 1] = '\0';
            g_api->save_all(g_api->ctx);
            g_api->nav(g_api->ctx, SubGhzConfigNavPrevious, SubGhzConfigSceneNone);
            return true;
        }
    }
    return false;
}

static void fileprefix_on_exit(void) {
    text_input_reset(g_api->get_text_input(g_api->ctx));
}

static const SubGhzConfigScenePlugin fileprefix_plugin = {
    .set_host_api = fileprefix_set_host_api,
    .on_enter = fileprefix_on_enter,
    .on_event = fileprefix_on_event,
    .on_exit = fileprefix_on_exit,
};

static const FlipperAppPluginDescriptor fileprefix_plugin_descriptor = {
    .appid = SUBGHZ_CONFIG_SCENE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_CONFIG_SCENE_PLUGIN_API_VERSION,
    .entry_point = &fileprefix_plugin,
};

const FlipperAppPluginDescriptor* subghz_config_fileprefix_ep(void) {
    return &fileprefix_plugin_descriptor;
}
