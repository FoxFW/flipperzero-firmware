#include "fox_freqmod_helper_i.h"
#include "helpers/fmh_subghz_launch.h"

#include <furi.h>
#include <string.h>

static bool fmh_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    FmhApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool fmh_back_event_callback(void* context) {
    furi_assert(context);
    FmhApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static FmhApp* fmh_app_alloc(void) {
    FmhApp* app = malloc(sizeof(FmhApp));
    memset(app, 0, sizeof(FmhApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&fmh_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, fmh_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, fmh_back_event_callback);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, FmhViewSubmenu, submenu_get_view(app->submenu));

    app->box_list = fmh_box_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, FmhViewBoxList, fmh_box_list_get_view(app->box_list));

    fmh_str_list_init(&app->makes);
    fmh_str_list_init(&app->models);
    fmh_year_list_init(&app->years);
    fmh_result_list_init(&app->results);

    scene_manager_next_scene(app->scene_manager, FmhSceneStart);

    return app;
}

static void fmh_app_free(FmhApp* app) {
    furi_assert(app);

    view_dispatcher_remove_view(app->view_dispatcher, FmhViewSubmenu);
    submenu_free(app->submenu);

    view_dispatcher_remove_view(app->view_dispatcher, FmhViewBoxList);
    fmh_box_list_free(app->box_list);

    fmh_str_list_free(&app->makes);
    fmh_str_list_free(&app->models);
    fmh_year_list_free(&app->years);
    fmh_result_list_free(&app->results);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);

    free(app);
}

int32_t fox_freqmod_helper_app(void* p) {
    UNUSED(p);
    FmhApp* app = fmh_app_alloc();

    view_dispatcher_run(app->view_dispatcher);

    bool launch = app->launch_requested;
    uint32_t freq = app->launch_freq;
    uint8_t preset = app->launch_preset;
    char mode = app->launch_mode;

    fmh_app_free(app);

    if(launch) {
        fmh_launch_subghz(freq, preset, mode);
    }

    return 0;
}
