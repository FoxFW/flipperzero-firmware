

#include "../helpers/subghz_config_scene.h"
#include "../helpers/subghz_custom_event.h"
#include <toolbox/path.h>

static const SubGhzConfigHostApi* g_api = NULL;

static void delete_button_callback(GuiButtonType result, InputType type, void* context) {
    UNUSED(context);
    if((result == GuiButtonTypeRight) && (type == InputTypeShort)) {
        view_dispatcher_send_custom_event(
            g_api->get_view_dispatcher(g_api->ctx), SubGhzCustomEventSceneDelete);
    } else if((result == GuiButtonTypeLeft) && (type == InputTypeShort)) {
        view_dispatcher_send_custom_event(
            g_api->get_view_dispatcher(g_api->ctx), SubGhzCustomEventSceneDeleteBack);
    }
}

static void delete_set_host_api(const SubGhzConfigHostApi* api) {
    g_api = api;
}

static void delete_on_enter(void) {
    Widget* widget = g_api->get_widget(g_api->ctx);

    FuriString* text_out = furi_string_alloc();
    FuriString* text = furi_string_alloc();

    path_extract_filename(g_api->get_file_path(g_api->ctx), text, true);
    furi_string_cat_printf(text_out, "\e#Delete %s?\e#\n", furi_string_get_cstr(text));

    furi_string_reset(text);
    g_api->get_decoder_string(g_api->ctx, text);

    size_t dot = furi_string_search_char(text, '\r');
    if(dot > 0) {
        furi_string_left(text, dot);
    }
    furi_string_cat_printf(text_out, "%s\n", furi_string_get_cstr(text));
    furi_string_free(text);

    FuriString* frequency_str = furi_string_alloc();
    FuriString* modulation_str = furi_string_alloc();
    g_api->get_freq_mod(g_api->ctx, frequency_str, modulation_str);
    furi_string_cat_printf(
        text_out,
        "%s %s",
        furi_string_get_cstr(frequency_str),
        furi_string_get_cstr(modulation_str));

    widget_add_text_box_element(
        widget, 0, 0, 128, 54, AlignCenter, AlignTop, furi_string_get_cstr(text_out), false);

    furi_string_free(frequency_str);
    furi_string_free(modulation_str);
    furi_string_free(text_out);

    widget_add_button_element(widget, GuiButtonTypeRight, "Delete", delete_button_callback, NULL);
    widget_add_button_element(widget, GuiButtonTypeLeft, "Cancel", delete_button_callback, NULL);

    g_api->switch_to_view(g_api->ctx, SubGhzConfigViewWidget);
}

static bool delete_on_event(SceneManagerEvent event) {
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubGhzCustomEventSceneDelete) {
            furi_string_set(g_api->get_file_path_tmp(g_api->ctx), g_api->get_file_path(g_api->ctx));
            if(g_api->delete_file(g_api->ctx)) {
                g_api->nav(g_api->ctx, SubGhzConfigNavNext, SubGhzConfigSceneDeleteSuccess);
            } else {
                g_api->nav(g_api->ctx, SubGhzConfigNavSearchPrev, SubGhzConfigSceneStart);
            }
            return true;
        } else if(event.event == SubGhzCustomEventSceneDeleteBack) {
            g_api->nav(g_api->ctx, SubGhzConfigNavPrevious, SubGhzConfigSceneNone);
            return true;
        }
    }
    return false;
}

static void delete_on_exit(void) {
    widget_reset(g_api->get_widget(g_api->ctx));
}

static const SubGhzConfigScenePlugin delete_plugin = {
    .set_host_api = delete_set_host_api,
    .on_enter = delete_on_enter,
    .on_event = delete_on_event,
    .on_exit = delete_on_exit,
};

static const FlipperAppPluginDescriptor delete_plugin_descriptor = {
    .appid = SUBGHZ_CONFIG_SCENE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_CONFIG_SCENE_PLUGIN_API_VERSION,
    .entry_point = &delete_plugin,
};

const FlipperAppPluginDescriptor* subghz_config_delete_ep(void) {
    return &delete_plugin_descriptor;
}
