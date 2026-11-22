

#include "../helpers/subghz_config_scene.h"
#include "../helpers/subghz_custom_event.h"

static const SubGhzConfigHostApi* g_api = NULL;

static void deletesuccess_popup_callback(void* context) {
    UNUSED(context);
    view_dispatcher_send_custom_event(
        g_api->get_view_dispatcher(g_api->ctx), SubGhzCustomEventSceneDeleteSuccess);
}

static void deletesuccess_set_host_api(const SubGhzConfigHostApi* api) {
    g_api = api;
}

static void deletesuccess_on_enter(void) {
    Popup* popup = g_api->get_popup(g_api->ctx);
    popup_set_header(popup, "Deleted", 80, 19, AlignLeft, AlignBottom);
    popup_set_timeout(popup, 1500);
    popup_set_context(popup, NULL);
    popup_set_callback(popup, deletesuccess_popup_callback);
    popup_enable_timeout(popup);
    g_api->switch_to_view(g_api->ctx, SubGhzConfigViewPopup);
}

static bool deletesuccess_on_event(SceneManagerEvent event) {
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubGhzCustomEventSceneDeleteSuccess) {
            if(g_api->get_rx_key_state(g_api->ctx) == SubGhzConfigRxKeyStateRAWLoad) {
                g_api->nav(g_api->ctx, SubGhzConfigNavSearchPrevOrStart, SubGhzConfigSceneSaved);
            } else {
                g_api->set_rx_key_state(g_api->ctx, SubGhzConfigRxKeyStateIDLE);
                g_api->nav(
                    g_api->ctx, SubGhzConfigNavSearchPrevOrStart, SubGhzConfigSceneReadRAW);
            }
            return true;
        }
    }
    return false;
}

static void deletesuccess_on_exit(void) {
    popup_reset(g_api->get_popup(g_api->ctx));
}

static const SubGhzConfigScenePlugin deletesuccess_plugin = {
    .set_host_api = deletesuccess_set_host_api,
    .on_enter = deletesuccess_on_enter,
    .on_event = deletesuccess_on_event,
    .on_exit = deletesuccess_on_exit,
};

static const FlipperAppPluginDescriptor deletesuccess_plugin_descriptor = {
    .appid = SUBGHZ_CONFIG_SCENE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_CONFIG_SCENE_PLUGIN_API_VERSION,
    .entry_point = &deletesuccess_plugin,
};

const FlipperAppPluginDescriptor* subghz_config_deletesuccess_ep(void) {
    return &deletesuccess_plugin_descriptor;
}
