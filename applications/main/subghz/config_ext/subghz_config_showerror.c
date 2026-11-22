

#include "../helpers/subghz_config_scene.h"
#include "../helpers/subghz_custom_event.h"

static const SubGhzConfigHostApi* g_api = NULL;

static void showerror_button_cb(GuiButtonType result, InputType type, void* context) {
    UNUSED(context);
    if((result == GuiButtonTypeRight) && (type == InputTypeShort)) {
        view_dispatcher_send_custom_event(
            g_api->get_view_dispatcher(g_api->ctx), SubGhzCustomEventSceneShowErrorOk);
    } else if((result == GuiButtonTypeLeft) && (type == InputTypeShort)) {
        view_dispatcher_send_custom_event(
            g_api->get_view_dispatcher(g_api->ctx), SubGhzCustomEventSceneShowErrorBack);
    }
}

static void showerror_set_host_api(const SubGhzConfigHostApi* api) {
    g_api = api;
}

static void showerror_on_enter(void) {
    Widget* widget = g_api->get_widget(g_api->ctx);

    g_api->widget_add_fw_icon(g_api->ctx, 0, 0, SubGhzConfigIconSDQuestion);

    widget_add_string_multiline_element(
        widget,
        81,
        24,
        AlignCenter,
        AlignCenter,
        FontSecondary,
        furi_string_get_cstr(g_api->get_error_str(g_api->ctx)));

    if(g_api->get_scene_state(g_api->ctx) == SubGhzCustomEventManagerSet) {
        widget_add_button_element(widget, GuiButtonTypeRight, "OK", showerror_button_cb, NULL);
    } else {
        g_api->notify_sd_error(g_api->ctx);
    }

    widget_add_button_element(widget, GuiButtonTypeLeft, "Back", showerror_button_cb, NULL);

    g_api->switch_to_view(g_api->ctx, SubGhzConfigViewWidget);
}

static bool showerror_on_event(SceneManagerEvent event) {
    uint32_t scene_state = g_api->get_scene_state(g_api->ctx);
    if(event.type == SceneManagerEventTypeBack) {
        if(scene_state == SubGhzCustomEventManagerSet) {
            return false;
        } else {
            g_api->nav(g_api->ctx, SubGhzConfigNavSearchPrev, SubGhzConfigSceneStart);
        }
        return true;
    } else if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubGhzCustomEventSceneShowErrorOk) {
            if(scene_state == SubGhzCustomEventManagerSet) {
                g_api->nav(g_api->ctx, SubGhzConfigNavNext, SubGhzConfigSceneStart);
            }
            return true;
        } else if(event.event == SubGhzCustomEventSceneShowErrorBack) {
            if(scene_state == SubGhzCustomEventManagerSet) {
                g_api->nav(g_api->ctx, SubGhzConfigNavPreviousOrStop, SubGhzConfigSceneNone);
            } else {
                g_api->nav(g_api->ctx, SubGhzConfigNavSearchPrev, SubGhzConfigSceneStart);
            }
            return true;
        }
    }
    return false;
}

static void showerror_on_exit(void) {
    g_api->set_scene_state_of(
        g_api->ctx, SubGhzConfigSceneShowError, SubGhzCustomEventManagerNoSet);
    widget_reset(g_api->get_widget(g_api->ctx));
    furi_string_reset(g_api->get_error_str(g_api->ctx));
    g_api->notify_reset_rgb(g_api->ctx);
}

static const SubGhzConfigScenePlugin showerror_plugin = {
    .set_host_api = showerror_set_host_api,
    .on_enter = showerror_on_enter,
    .on_event = showerror_on_event,
    .on_exit = showerror_on_exit,
};

static const FlipperAppPluginDescriptor showerror_plugin_descriptor = {
    .appid = SUBGHZ_CONFIG_SCENE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_CONFIG_SCENE_PLUGIN_API_VERSION,
    .entry_point = &showerror_plugin,
};

const FlipperAppPluginDescriptor* subghz_config_showerror_ep(void) {
    return &showerror_plugin_descriptor;
}
