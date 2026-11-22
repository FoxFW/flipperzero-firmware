#include "../saflok_app_i.h"

#undef TAG
#define TAG "SaflokSceneMfClassicWriteInitialFail"

void saflok_scene_mf_classic_write_initial_fail_widget_callback(
    GuiButtonType result,
    InputType type,
    void* context) {
    SaflokApp* app = context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, result);
    }
}

void saflok_scene_mf_classic_write_initial_fail_on_enter(void* context) {
    SaflokApp* app = context;
    
    FURI_LOG_D(TAG, "on_enter");

    Widget* widget = app->widget;

    notification_message(app->notifications, &sequence_error);

    widget_add_icon_element(widget, 83, 22, &I_WarningDolphinFlip_45x42);
    widget_add_string_element(
        widget, 7, 4, AlignLeft, AlignTop, FontPrimary, "Writing gone wrong!");
    widget_add_string_multiline_element(
        widget,
        7,
        17,
        AlignLeft,
        AlignTop,
        FontSecondary,
        "Not all sectors\nwere written\ncorrectly.");

    widget_add_button_element(
        widget,
        GuiButtonTypeLeft,
        "Finish",
        saflok_scene_mf_classic_write_initial_fail_widget_callback,
        app);

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewWidget);
}

bool saflok_scene_mf_classic_write_initial_fail_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    
    FURI_LOG_D(TAG, "on_event");
    
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == GuiButtonTypeLeft) {
            consumed = scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, SaflokSceneTopLevel);
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        consumed = scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, SaflokSceneTopLevel);
    }
    return consumed;
}

void saflok_scene_mf_classic_write_initial_fail_on_exit(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_exit");
    
    widget_reset(app->widget);
}
