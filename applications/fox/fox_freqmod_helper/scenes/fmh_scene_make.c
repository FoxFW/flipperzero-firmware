#include "../fox_freqmod_helper_i.h"

static void fmh_scene_make_submenu_cb(void* context, uint32_t index) {
    FmhApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void fmh_scene_make_on_enter(void* context) {
    FmhApp* app = context;

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Select Make");

    fmh_data_load_makes(&app->makes);

    if(app->makes.count == 0) {
        submenu_add_item(
            app->submenu, "No vehicle data found", 0, fmh_scene_make_submenu_cb, app);
    } else {
        for(uint16_t i = 0; i < app->makes.count; i++) {
            submenu_add_item(app->submenu, app->makes.items[i], i, fmh_scene_make_submenu_cb, app);
        }
        if(app->make_set) {
            int32_t idx = fmh_str_list_index_of(&app->makes, app->make);
            if(idx >= 0) submenu_set_selected_item(app->submenu, (uint32_t)idx);
        }
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, FmhViewSubmenu);
}

bool fmh_scene_make_on_event(void* context, SceneManagerEvent event) {
    FmhApp* app = context;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom) {
        if(app->makes.count == 0) {
            scene_manager_previous_scene(app->scene_manager);
            consumed = true;
        } else if(event.event < app->makes.count) {
            const char* chosen = app->makes.items[event.event];
            if(!app->make_set || strcmp(app->make, chosen) != 0) {
                app->model_set = false;
            }
            strncpy(app->make, chosen, FMH_NAME_MAX - 1);
            app->make[FMH_NAME_MAX - 1] = '\0';
            app->make_set = true;
            fmh_data_load_models(app->make, &app->models);
            if(app->models.count == 0) {
                app->make_direct = true;
                app->model[0] = '\0';
                app->model_set = false;
                app->year_from = 0;
                app->year_to = 0;
                app->year_index = 0;
                scene_manager_next_scene(app->scene_manager, FmhSceneResult);
            } else {
                app->make_direct = false;
                scene_manager_previous_scene(app->scene_manager);
            }
            consumed = true;
        }
    }
    return consumed;
}

void fmh_scene_make_on_exit(void* context) {
    FmhApp* app = context;
    submenu_reset(app->submenu);
}
