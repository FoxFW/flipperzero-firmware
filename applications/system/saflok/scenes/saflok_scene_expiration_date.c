#include "../saflok_app_i.h"
#include "../helpers/saflok_app_helpers.h"
#include "../helpers/saflok_field_helpers.h"

void saflok_scene_expiration_date_on_enter(void* context) {
    SaflokApp* app = context;
    DateInput* saflok_date_input = app->date_input;

    with_view_model(saflok_date_input->view, DateInputModel * model, {
        date_input_init_from_date(model, &app->saflok_data->time_control.expiration_date);
    }, true);

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewDateInput);
}

bool saflok_scene_expiration_date_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeBack) {
        with_view_model(app->date_input->view, DateInputModel * model, {
            SaflokDateTime new_date;
            date_input_extract_to_date(model, &new_date);
            
            if (date_has_changed(&app->saflok_data->time_control.expiration_date, &new_date)) {
                app->saflok_data->time_control.expiration_date = new_date;
                
                app->changed->expiration_date = true;
            }
        }, false);

        consumed = scene_manager_previous_scene(app->scene_manager);
    }

    return consumed;
}

void saflok_scene_expiration_date_on_exit(void* context) {
    SaflokApp* app = context;
    if(app->changed->expiration_date) {
        saflok_pack_current_expiration_date(app);
    }
    saflok_date_input_reset(app->date_input);
}
