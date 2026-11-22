

#include "../helpers/subghz_config_scene.h"
#include "../helpers/subghz_custom_event.h"

static const SubGhzConfigHostApi* g_api = NULL;

static void showerrorsub_popup_callback(void* context) {
    UNUSED(context);
    view_dispatcher_send_custom_event(
        g_api->get_view_dispatcher(g_api->ctx), SubGhzCustomEventSceneShowErrorSub);
}

static void showerrorsub_set_host_api(const SubGhzConfigHostApi* api) {
    g_api = api;
}

static void showerrorsub_on_enter(void) {
    Popup* popup = g_api->get_popup(g_api->ctx);

    popup_set_header(
        popup, furi_string_get_cstr(g_api->get_error_str(g_api->ctx)), 14, 15, AlignLeft, AlignTop);
    popup_set_timeout(popup, 1500);
    popup_set_context(popup, NULL);
    popup_set_callback(popup, showerrorsub_popup_callback);
    popup_enable_timeout(popup);
    g_api->switch_to_view(g_api->ctx, SubGhzConfigViewPopup);

    g_api->notify_set_red(g_api->ctx);
}

static bool showerrorsub_on_event(SceneManagerEvent event) {
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubGhzCustomEventSceneShowErrorSub) {
            g_api->nav(g_api->ctx, SubGhzConfigNavSearchPrev, SubGhzConfigSceneStart);
            return true;
        }
    }
    return false;
}

static void showerrorsub_on_exit(void) {
    popup_reset(g_api->get_popup(g_api->ctx));
    furi_string_reset(g_api->get_error_str(g_api->ctx));
    g_api->notify_reset_rgb(g_api->ctx);
}

static const SubGhzConfigScenePlugin showerrorsub_plugin = {
    .set_host_api = showerrorsub_set_host_api,
    .on_enter = showerrorsub_on_enter,
    .on_event = showerrorsub_on_event,
    .on_exit = showerrorsub_on_exit,
};

static const FlipperAppPluginDescriptor showerrorsub_plugin_descriptor = {
    .appid = SUBGHZ_CONFIG_SCENE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_CONFIG_SCENE_PLUGIN_API_VERSION,
    .entry_point = &showerrorsub_plugin,
};

const FlipperAppPluginDescriptor* subghz_config_showerrorsub_ep(void) {
    return &showerrorsub_plugin_descriptor;
}
