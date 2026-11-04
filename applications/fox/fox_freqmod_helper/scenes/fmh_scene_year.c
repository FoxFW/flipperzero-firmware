#include "../fox_freqmod_helper_i.h"

static void fmh_scene_year_submenu_cb(void* context, uint32_t index) {
    FmhApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void fmh_scene_year_on_enter(void* context) {
    FmhApp* app = context;

    submenu_reset(app->submenu);

    char header[FMH_HEADER_MAX];
    snprintf(header, sizeof(header), "%s %s", app->make, app->model);
    submenu_set_header(app->submenu, header);

    for(uint16_t i = 0; i < app->years.count; i++) {
        char label[24];
        fmh_format_year(
            app->years.items[i].year_from, app->years.items[i].year_to, label, sizeof(label));
        submenu_add_item(app->submenu, label, i, fmh_scene_year_submenu_cb, app);
    }
    if(app->year_index < app->years.count) {
        submenu_set_selected_item(app->submenu, app->year_index);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, FmhViewSubmenu);
}

bool fmh_scene_year_on_event(void* context, SceneManagerEvent event) {
    FmhApp* app = context;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event < app->years.count) {
            app->year_index = (uint16_t)event.event;
            app->year_from = app->years.items[event.event].year_from;
            app->year_to = app->years.items[event.event].year_to;
            scene_manager_next_scene(app->scene_manager, FmhSceneResult);
            consumed = true;
        }
    }
    return consumed;
}

void fmh_scene_year_on_exit(void* context) {
    FmhApp* app = context;
    submenu_reset(app->submenu);
}
