#include "../saflok_app_i.h"

void saflok_scene_update_initial_wrong_card_widget_callback(
    GuiButtonType result,
    InputType type,
    void* context) {
    SaflokApp* app = context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, result);
    }
}

void saflok_scene_update_initial_wrong_card_on_enter(void* context) {
    SaflokApp* app = context;
    Widget* widget = app->widget;

    notification_message(app->notifications, &sequence_error);

    widget_add_icon_element(widget, 83, 22, &I_WarningDolphinFlip_45x42);
    widget_add_string_element(widget, 3, 4, AlignLeft, AlignTop, FontPrimary, "Wrong Card!");
    widget_add_string_multiline_element(
        widget,
        4,
        17,
        AlignLeft,
        AlignTop,
        FontSecondary,
        "Data management\nis only possible\nwith source card");
    widget_add_button_element(
        widget,
        GuiButtonTypeLeft,
        "Retry",
        saflok_scene_update_initial_wrong_card_widget_callback,
        app);

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewWidget);
}

bool saflok_scene_update_initial_wrong_card_on_event(
    void* context,
    SceneManagerEvent event) {
    SaflokApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == GuiButtonTypeLeft) {
            consumed = scene_manager_previous_scene(app->scene_manager);
        }
    }
    return consumed;
}

void saflok_scene_update_initial_wrong_card_on_exit(void* context) {
    SaflokApp* app = context;

    widget_reset(app->widget);
}
