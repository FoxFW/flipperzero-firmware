#include "pocsag_pager_app_i.h"

#include <furi.h>
#include <furi_hal.h>
#include <lib/flipper_format/flipper_format.h>
#include "protocols/protocol_items.h"

static bool pocsag_pager_app_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    POCSAGPagerApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool pocsag_pager_app_back_event_callback(void* context) {
    furi_assert(context);
    POCSAGPagerApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void pocsag_pager_app_tick_event_callback(void* context) {
    furi_assert(context);
    POCSAGPagerApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

POCSAGPagerApp* pocsag_pager_app_alloc() {
    POCSAGPagerApp* app = malloc(sizeof(POCSAGPagerApp));

    app->gui = furi_record_open(RECORD_GUI);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&pocsag_pager_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, pocsag_pager_app_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, pocsag_pager_app_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, pocsag_pager_app_tick_event_callback, 100);

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->variable_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        POCSAGPagerViewVariableItemList,
        variable_item_list_get_view(app->variable_item_list));

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, POCSAGPagerViewSubmenu, submenu_get_view(app->submenu));

    app->widget = widget_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, POCSAGPagerViewWidget, widget_get_view(app->widget));

    app->pcsg_receiver = pcsg_view_receiver_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        POCSAGPagerViewReceiver,
        pcsg_view_receiver_get_view(app->pcsg_receiver));

    app->pcsg_receiver_info = pcsg_view_receiver_info_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        POCSAGPagerViewReceiverInfo,
        pcsg_view_receiver_info_get_view(app->pcsg_receiver_info));

    app->setting = subghz_setting_alloc();

    subghz_setting_load(app->setting, EXT_PATH("subghz/assets/setting_user.pocsag"));

    app->lock = PCSGLockOff;
    app->txrx = malloc(sizeof(POCSAGPagerTxRx));
    app->txrx->preset = malloc(sizeof(SubGhzRadioPreset));
    app->txrx->preset->name = furi_string_alloc();

    furi_hal_power_suppress_charge_enter();

    subghz_devices_init();
    app->txrx->radio_device =
        radio_device_loader_set(app->txrx->radio_device, SubGhzRadioDeviceTypeExternalCC1101);

    subghz_devices_reset(app->txrx->radio_device);
    subghz_devices_idle(app->txrx->radio_device);

    FlipperFormat* temp_fm_preset = flipper_format_string_alloc();
    flipper_format_write_string_cstr(
        temp_fm_preset,
        (const char*)"Custom_preset_data",
        (const char*)"02 0D 0B 06 08 32 07 04 14 00 13 02 12 04 11 83 10 67 15 24 18 18 19 16 1D 91 1C 00 1B 07 20 FB 22 10 21 56 00 00 C0 00 00 00 00 00 00 00");
    flipper_format_rewind(temp_fm_preset);
    subghz_setting_load_custom_preset(app->setting, (const char*)"FM95", temp_fm_preset);

    flipper_format_free(temp_fm_preset);

    pcsg_preset_init(app, "FM95", 439987500, NULL, 0);

    app->txrx->hopper_state = PCSGHopperStateOFF;
    app->txrx->history = pcsg_history_alloc();
    app->txrx->worker = subghz_worker_alloc();
    app->txrx->environment = subghz_environment_alloc();
    subghz_environment_set_protocol_registry(
        app->txrx->environment, (void*)&pocsag_pager_protocol_registry);
    app->txrx->receiver = subghz_receiver_alloc_init(app->txrx->environment);

    subghz_receiver_set_filter(app->txrx->receiver, SubGhzProtocolFlag_Decodable);
    subghz_worker_set_overrun_callback(
        app->txrx->worker, (SubGhzWorkerOverrunCallback)subghz_receiver_reset);
    subghz_worker_set_pair_callback(
        app->txrx->worker, (SubGhzWorkerPairCallback)subghz_receiver_decode);
    subghz_worker_set_context(app->txrx->worker, app->txrx->receiver);

    scene_manager_next_scene(app->scene_manager, POCSAGPagerSceneStart);

    return app;
}

void pocsag_pager_app_free(POCSAGPagerApp* app) {
    furi_assert(app);

    pcsg_sleep(app);
    radio_device_loader_end(app->txrx->radio_device);

    subghz_devices_deinit();

    view_dispatcher_remove_view(app->view_dispatcher, POCSAGPagerViewSubmenu);
    submenu_free(app->submenu);

    view_dispatcher_remove_view(app->view_dispatcher, POCSAGPagerViewVariableItemList);
    variable_item_list_free(app->variable_item_list);

    view_dispatcher_remove_view(app->view_dispatcher, POCSAGPagerViewWidget);
    widget_free(app->widget);

    view_dispatcher_remove_view(app->view_dispatcher, POCSAGPagerViewReceiver);
    pcsg_view_receiver_free(app->pcsg_receiver);

    view_dispatcher_remove_view(app->view_dispatcher, POCSAGPagerViewReceiverInfo);
    pcsg_view_receiver_info_free(app->pcsg_receiver_info);

    subghz_setting_free(app->setting);

    subghz_receiver_free(app->txrx->receiver);
    subghz_environment_free(app->txrx->environment);
    pcsg_history_free(app->txrx->history);
    subghz_worker_free(app->txrx->worker);
    furi_string_free(app->txrx->preset->name);
    free(app->txrx->preset);
    free(app->txrx);

    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    furi_record_close(RECORD_NOTIFICATION);
    app->notifications = NULL;

    furi_record_close(RECORD_GUI);

    furi_hal_power_suppress_charge_exit();

    free(app);
}

int32_t pocsag_pager_app(void* p) {
    UNUSED(p);
    POCSAGPagerApp* pocsag_pager_app = pocsag_pager_app_alloc();

    view_dispatcher_run(pocsag_pager_app->view_dispatcher);

    pocsag_pager_app_free(pocsag_pager_app);

    return 0;
}
