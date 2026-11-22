

#include "../helpers/subghz_config_scene.h"
#include "../helpers/subghz_custom_event.h"

static const SubGhzConfigHostApi* g_api = NULL;

static void needsaving_button_callback(GuiButtonType result, InputType type, void* context) {
    UNUSED(context);
    if((result == GuiButtonTypeRight) && (type == InputTypeShort)) {
        view_dispatcher_send_custom_event(
            g_api->get_view_dispatcher(g_api->ctx), SubGhzCustomEventSceneStay);
    } else if((result == GuiButtonTypeLeft) && (type == InputTypeShort)) {
        view_dispatcher_send_custom_event(
            g_api->get_view_dispatcher(g_api->ctx), SubGhzCustomEventSceneExit);
    }
}

static void needsaving_set_host_api(const SubGhzConfigHostApi* api) {
    g_api = api;
}

static void needsaving_on_enter(void) {
    Widget* widget = g_api->get_widget(g_api->ctx);
    widget_add_string_multiline_element(
        widget, 64, 13, AlignCenter, AlignCenter, FontPrimary, "Exit to Sub-GHz Menu?");
    widget_add_string_multiline_element(
        widget, 64, 32, AlignCenter, AlignCenter, FontSecondary, "All unsaved data\nwill be lost!");
    widget_add_button_element(widget, GuiButtonTypeRight, "Stay", needsaving_button_callback, NULL);
    widget_add_button_element(widget, GuiButtonTypeLeft, "Exit", needsaving_button_callback, NULL);
    g_api->switch_to_view(g_api->ctx, SubGhzConfigViewWidget);
}

static bool needsaving_on_event(SceneManagerEvent event) {
    if(event.type == SceneManagerEventTypeBack) {
        g_api->set_rx_key_state(g_api->ctx, SubGhzConfigRxKeyStateBack);
        g_api->nav(g_api->ctx, SubGhzConfigNavPrevious, SubGhzConfigSceneNone);
        return true;
    } else if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubGhzCustomEventSceneStay) {
            g_api->set_rx_key_state(g_api->ctx, SubGhzConfigRxKeyStateBack);
            g_api->nav(g_api->ctx, SubGhzConfigNavPrevious, SubGhzConfigSceneNone);
            return true;
        } else if(event.event == SubGhzCustomEventSceneExit) {
            SubGhzConfigRxKeyState state = g_api->get_rx_key_state(g_api->ctx);
            g_api->set_rx_key_state(g_api->ctx, SubGhzConfigRxKeyStateIDLE);
            if(state == SubGhzConfigRxKeyStateExit) {
                if(g_api->has_previous_scene(g_api->ctx, SubGhzConfigSceneReadRAW)) {
                    if(!furi_string_empty(g_api->get_file_path_tmp(g_api->ctx))) {
                        g_api->delete_file(g_api->ctx);
                    }
                }
                g_api->reset_preset_am650(g_api->ctx);
                g_api->nav(g_api->ctx, SubGhzConfigNavSearchPrevOrPrevious, SubGhzConfigSceneStart);
            } else {
                g_api->nav(g_api->ctx, SubGhzConfigNavPrevious, SubGhzConfigSceneNone);
            }
            return true;
        }
    }
    return false;
}

static void needsaving_on_exit(void) {
    widget_reset(g_api->get_widget(g_api->ctx));
}

static const SubGhzConfigScenePlugin needsaving_plugin = {
    .set_host_api = needsaving_set_host_api,
    .on_enter = needsaving_on_enter,
    .on_event = needsaving_on_event,
    .on_exit = needsaving_on_exit,
};

static const FlipperAppPluginDescriptor needsaving_plugin_descriptor = {
    .appid = SUBGHZ_CONFIG_SCENE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_CONFIG_SCENE_PLUGIN_API_VERSION,
    .entry_point = &needsaving_plugin,
};

const FlipperAppPluginDescriptor* subghz_config_needsaving_ep(void) {
    return &needsaving_plugin_descriptor;
}
