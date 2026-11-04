#include "../fox_freqmod_helper_i.h"

static void fmh_to_upper(const char* src, char* dst, size_t dst_size) {
    size_t i = 0;
    for(; src[i] != '\0' && i < dst_size - 1; i++) {
        char c = src[i];
        if(c >= 'a' && c <= 'z') c = (char)(c - 32);
        dst[i] = c;
    }
    dst[i] = '\0';
}

static void fmh_scene_result_box_cb(void* context, uint32_t index) {
    FmhApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void fmh_scene_result_on_enter(void* context) {
    FmhApp* app = context;

    fmh_data_load_results(app->make, app->model, app->year_from, app->year_to, &app->results);

    if(app->model[0]) {
        snprintf(app->result_header, sizeof(app->result_header), "%s %s", app->make, app->model);
    } else {
        snprintf(app->result_header, sizeof(app->result_header), "%s", app->make);
    }

    uint8_t count = (app->results.count > FMH_RESULT_MAX) ? FMH_RESULT_MAX :
                                                            (uint8_t)app->results.count;

    for(uint8_t i = 0; i < count; i++) {
        FmhResult* r = &app->results.items[i];
        fmh_to_upper(r->region, app->result_titles[i], FMH_REGION_LEN);
        char mhz[12];
        fmh_format_mhz(r->freq, mhz, sizeof(mhz));
        snprintf(
            app->result_subs[i], FMH_SUB_MAX, "FREQ: %s  MOD: %s", mhz, fmh_preset_name(r->preset));
        app->result_options[i].title = app->result_titles[i];
        app->result_options[i].subtitle = app->result_subs[i];
    }

    fmh_box_list_set_header(app->box_list, app->result_header);
    fmh_box_list_set_callback(app->box_list, fmh_scene_result_box_cb, app);
    fmh_box_list_set_options(app->box_list, app->result_options, count);
    fmh_box_list_set_selected(app->box_list, 0);
    fmh_box_list_resume_scroll(app->box_list);

    view_dispatcher_switch_to_view(app->view_dispatcher, FmhViewBoxList);
}

bool fmh_scene_result_on_event(void* context, SceneManagerEvent event) {
    FmhApp* app = context;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom) {
        uint8_t count = (app->results.count > FMH_RESULT_MAX) ? FMH_RESULT_MAX :
                                                                (uint8_t)app->results.count;
        if(event.event < count) {
            app->launch_freq = app->results.items[event.event].freq;
            app->launch_preset = app->results.items[event.event].preset;
            app->launch_mode = app->results.items[event.event].launch;
            app->launch_requested = true;
            scene_manager_stop(app->scene_manager);
            view_dispatcher_stop(app->view_dispatcher);
            consumed = true;
        }
    }
    return consumed;
}

void fmh_scene_result_on_exit(void* context) {
    FmhApp* app = context;
    fmh_box_list_pause_scroll(app->box_list);
}
