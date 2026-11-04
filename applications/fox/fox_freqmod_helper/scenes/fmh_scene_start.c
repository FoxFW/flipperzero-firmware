#include "../fox_freqmod_helper_i.h"

static void fmh_scene_start_box_cb(void* context, uint32_t index) {
    FmhApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void fmh_scene_start_on_enter(void* context) {
    FmhApp* app = context;

    app->start_options[0].title = "MAKE";
    if(app->make_set) {
        strncpy(app->start_make_sub, app->make, FMH_NAME_MAX - 1);
        app->start_make_sub[FMH_NAME_MAX - 1] = '\0';
        app->start_options[0].subtitle = app->start_make_sub;
    } else {
        app->start_options[0].subtitle = "Select Make of Car";
    }

    uint8_t count = 1;
    bool show_model = app->make_set && !app->make_direct;
    if(show_model) {
        app->start_options[1].title = "MODEL";
        app->start_options[1].subtitle = "Select Model of Car";
        count = 2;
    }

    fmh_box_list_set_header(app->box_list, "Fox FreqMod Helper");
    fmh_box_list_set_callback(app->box_list, fmh_scene_start_box_cb, app);
    fmh_box_list_set_options(app->box_list, app->start_options, count);
    fmh_box_list_set_selected(app->box_list, show_model ? 1 : 0);
    fmh_box_list_resume_scroll(app->box_list);

    view_dispatcher_switch_to_view(app->view_dispatcher, FmhViewBoxList);
}

bool fmh_scene_start_on_event(void* context, SceneManagerEvent event) {
    FmhApp* app = context;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == 0) {
            scene_manager_next_scene(app->scene_manager, FmhSceneMake);
            consumed = true;
        } else if(event.event == 1 && app->make_set) {
            scene_manager_next_scene(app->scene_manager, FmhSceneModel);
            consumed = true;
        }
    }
    return consumed;
}

void fmh_scene_start_on_exit(void* context) {
    FmhApp* app = context;
    fmh_box_list_pause_scroll(app->box_list);
}
