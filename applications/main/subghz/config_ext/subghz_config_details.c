

#include "../helpers/subghz_config_scene.h"
#include <string.h>

static const SubGhzConfigHostApi* g_api = NULL;

static void details_set_host_api(const SubGhzConfigHostApi* api) {
    g_api = api;
}

static void details_on_enter(void) {
    Widget* widget = g_api->get_widget(g_api->ctx);
    widget_reset(widget);

    const char* full = furi_string_get_cstr(g_api->get_error_str(g_api->ctx));

    char proto[48] = "Details";
    const char* body = full;
    const char* nl = strchr(full, '\n');
    if(nl) {
        size_t len = (size_t)(nl - full);
        if(len >= sizeof(proto)) len = sizeof(proto) - 1;
        memcpy(proto, full, len);
        proto[len] = '\0';
        body = nl + 1;
    }

    widget_add_string_element(widget, 64, 2, AlignCenter, AlignTop, FontPrimary, proto);

    FuriString* body_str = furi_string_alloc_set_str(body);
    furi_string_trim(body_str);
    widget_add_text_scroll_element(widget, 0, 14, 128, 50, furi_string_get_cstr(body_str));
    furi_string_free(body_str);

    g_api->switch_to_view(g_api->ctx, SubGhzConfigViewWidget);
}

static bool details_on_event(SceneManagerEvent event) {
    UNUSED(event);
    return false;
}

static void details_on_exit(void) {
    widget_reset(g_api->get_widget(g_api->ctx));
}

static const SubGhzConfigScenePlugin details_plugin = {
    .set_host_api = details_set_host_api,
    .on_enter = details_on_enter,
    .on_event = details_on_event,
    .on_exit = details_on_exit,
};

static const FlipperAppPluginDescriptor details_plugin_descriptor = {
    .appid = SUBGHZ_CONFIG_SCENE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_CONFIG_SCENE_PLUGIN_API_VERSION,
    .entry_point = &details_plugin,
};

const FlipperAppPluginDescriptor* subghz_config_details_ep(void) {
    return &details_plugin_descriptor;
}
