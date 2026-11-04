#include "../fox_freqmod_helper_i.h"

static void fmh_scene_model_submenu_cb(void* context, uint32_t index) {
    FmhApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void fmh_scene_model_on_enter(void* context) {
    FmhApp* app = context;

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, app->make);

    fmh_data_load_models(app->make, &app->models);

    for(uint16_t i = 0; i < app->models.count; i++) {
        submenu_add_item(app->submenu, app->models.items[i], i, fmh_scene_model_submenu_cb, app);
    }
    if(app->model_set) {
        int32_t idx = fmh_str_list_index_of(&app->models, app->model);
        if(idx >= 0) submenu_set_selected_item(app->submenu, (uint32_t)idx);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, FmhViewSubmenu);
}

bool fmh_scene_model_on_event(void* context, SceneManagerEvent event) {
    FmhApp* app = context;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event < app->models.count) {
            strncpy(app->model, app->models.items[event.event], FMH_NAME_MAX - 1);
            app->model[FMH_NAME_MAX - 1] = '\0';
            app->model_set = true;

            fmh_data_load_years(app->make, app->model, &app->years);
            if(app->years.count <= 1) {
                if(app->years.count == 1) {
                    app->year_from = app->years.items[0].year_from;
                    app->year_to = app->years.items[0].year_to;
                } else {
                    app->year_from = 0;
                    app->year_to = 0;
                }
                app->year_index = 0;
                scene_manager_next_scene(app->scene_manager, FmhSceneResult);
            } else {
                scene_manager_next_scene(app->scene_manager, FmhSceneYear);
            }
            consumed = true;
        }
    }
    return consumed;
}

void fmh_scene_model_on_exit(void* context) {
    FmhApp* app = context;
    submenu_reset(app->submenu);
}
