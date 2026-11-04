#include "../tpms_app_i.h"

typedef enum {
    TPMSVehicleStepsEventNext,
} TPMSVehicleStepsEvent;

static bool tpms_scene_vehicle_steps_group_valid(TPMSApp* app) {
    return app->active_vehicle_group >= 0 && app->active_vehicle_group < TPMS_VEHICLE_GROUP_COUNT;
}

static void
    tpms_scene_vehicle_steps_widget_callback(GuiButtonType result, InputType type, void* context) {
    TPMSApp* app = context;
    if(type != InputTypeShort) return;
    if(result == GuiButtonTypeCenter) {
        view_dispatcher_send_custom_event(app->view_dispatcher, TPMSVehicleStepsEventNext);
    }
}

static void tpms_scene_vehicle_steps_show(TPMSApp* app) {
    widget_reset(app->widget);

    if(!tpms_scene_vehicle_steps_group_valid(app)) {

        app->active_vehicle_group = -1;
        scene_manager_next_scene(app->scene_manager, TPMSSceneReceiver);
        return;
    }

    const TPMSVehicleGroup* group = &tpms_vehicle_groups[app->active_vehicle_group];
    uint8_t step_count = group->step_count ? group->step_count : 1;
    if(app->vehicle_step_index >= step_count) {
        app->vehicle_step_index = step_count - 1;
    }
    const TPMSVehicleStep* step = &group->steps[app->vehicle_step_index];
    bool is_last_step = (app->vehicle_step_index + 1) >= step_count;

    widget_add_string_multiline_element(
        app->widget, 64, 6, AlignCenter, AlignTop, FontPrimary, step->heading);
    widget_add_text_scroll_element(app->widget, 0, 18, 128, 29, step->body);
    widget_add_button_element(
        app->widget,
        GuiButtonTypeCenter,
        is_last_step ? "Start Reading" : "Next",
        tpms_scene_vehicle_steps_widget_callback,
        app);

    view_dispatcher_switch_to_view(app->view_dispatcher, TPMSViewWidget);
}

void tpms_scene_vehicle_steps_on_enter(void* context) {
    TPMSApp* app = context;
    tpms_scene_vehicle_steps_show(app);
}

bool tpms_scene_vehicle_steps_on_event(void* context, SceneManagerEvent event) {
    TPMSApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == TPMSVehicleStepsEventNext) {
            const TPMSVehicleGroup* group = tpms_scene_vehicle_steps_group_valid(app) ?
                                                 &tpms_vehicle_groups[app->active_vehicle_group] :
                                                 NULL;
            bool is_last_step = !group || ((app->vehicle_step_index + 1) >= group->step_count);
            if(is_last_step) {

                tpms_relearn_lf_start(app);
                scene_manager_next_scene(app->scene_manager, TPMSSceneReceiver);
            } else {
                app->vehicle_step_index++;
                tpms_scene_vehicle_steps_show(app);
            }
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        if(app->vehicle_step_index > 0) {
            app->vehicle_step_index--;
            tpms_scene_vehicle_steps_show(app);
            consumed = true;
        }

    }

    return consumed;
}

void tpms_scene_vehicle_steps_on_exit(void* context) {
    TPMSApp* app = context;
    widget_reset(app->widget);
}
