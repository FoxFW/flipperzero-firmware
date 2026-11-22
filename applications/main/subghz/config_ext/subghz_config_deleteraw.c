

#include "../helpers/subghz_config_scene.h"
#include "../helpers/subghz_custom_event.h"
#include <toolbox/path.h>

static const SubGhzConfigHostApi* g_api = NULL;

static void deleteraw_button_callback(GuiButtonType result, InputType type, void* context) {
    UNUSED(context);
    if((result == GuiButtonTypeRight) && (type == InputTypeShort)) {
        view_dispatcher_send_custom_event(
            g_api->get_view_dispatcher(g_api->ctx), SubGhzCustomEventSceneDeleteRAW);
    } else if((result == GuiButtonTypeLeft) && (type == InputTypeShort)) {
        view_dispatcher_send_custom_event(
            g_api->get_view_dispatcher(g_api->ctx), SubGhzCustomEventSceneDeleteRAWBack);
    }
}

static void deleteraw_set_host_api(const SubGhzConfigHostApi* api) {
    g_api = api;
}

static void deleteraw_on_enter(void) {
    Widget* widget = g_api->get_widget(g_api->ctx);
    FuriString* frequency_str = furi_string_alloc();
    FuriString* modulation_str = furi_string_alloc();

    FuriString* file_name = furi_string_alloc();
    FuriString* delete_str = furi_string_alloc();
    path_extract_filename(g_api->get_file_path(g_api->ctx), file_name, true);
    furi_string_printf(delete_str, "\e#Delete %s?\e#", furi_string_get_cstr(file_name));
    furi_string_free(file_name);

    widget_add_text_box_element(
        widget, 0, 0, 128, 23, AlignCenter, AlignCenter, furi_string_get_cstr(delete_str), false);
    furi_string_free(delete_str);

    widget_add_string_element(widget, 38, 25, AlignLeft, AlignTop, FontSecondary, "RAW signal");
    g_api->get_freq_mod(g_api->ctx, frequency_str, modulation_str);
    widget_add_string_element(
        widget, 35, 37, AlignLeft, AlignTop, FontSecondary, furi_string_get_cstr(frequency_str));
    widget_add_string_element(
        widget, 72, 37, AlignLeft, AlignTop, FontSecondary, furi_string_get_cstr(modulation_str));

    furi_string_free(frequency_str);
    furi_string_free(modulation_str);

    widget_add_button_element(widget, GuiButtonTypeRight, "Delete", deleteraw_button_callback, NULL);
    widget_add_button_element(widget, GuiButtonTypeLeft, "Back", deleteraw_button_callback, NULL);

    g_api->switch_to_view(g_api->ctx, SubGhzConfigViewWidget);
}

static bool deleteraw_on_event(SceneManagerEvent event) {
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubGhzCustomEventSceneDeleteRAW) {
            furi_string_set(g_api->get_file_path_tmp(g_api->ctx), g_api->get_file_path(g_api->ctx));
            if(g_api->delete_file(g_api->ctx)) {
                g_api->nav(g_api->ctx, SubGhzConfigNavNext, SubGhzConfigSceneDeleteSuccess);
            } else {
                g_api->nav(g_api->ctx, SubGhzConfigNavSearchPrev, SubGhzConfigSceneStart);
            }
            return true;
        } else if(event.event == SubGhzCustomEventSceneDeleteRAWBack) {
            g_api->nav(g_api->ctx, SubGhzConfigNavPrevious, SubGhzConfigSceneNone);
            return true;
        }
    }
    return false;
}

static void deleteraw_on_exit(void) {
    widget_reset(g_api->get_widget(g_api->ctx));
}

static const SubGhzConfigScenePlugin deleteraw_plugin = {
    .set_host_api = deleteraw_set_host_api,
    .on_enter = deleteraw_on_enter,
    .on_event = deleteraw_on_event,
    .on_exit = deleteraw_on_exit,
};

static const FlipperAppPluginDescriptor deleteraw_plugin_descriptor = {
    .appid = SUBGHZ_CONFIG_SCENE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_CONFIG_SCENE_PLUGIN_API_VERSION,
    .entry_point = &deleteraw_plugin,
};

const FlipperAppPluginDescriptor* subghz_config_deleteraw_ep(void) {
    return &deleteraw_plugin_descriptor;
}
