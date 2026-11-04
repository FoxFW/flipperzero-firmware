#include "../tpms_app_i.h"

typedef enum {
    TPMSSceneRelearnEventTrigger,
} TPMSSceneRelearnEvent;

static void
    tpms_scene_relearn_widget_callback(GuiButtonType result, InputType type, void* context) {
    TPMSApp* app = context;
    if(type != InputTypeShort) return;
    if(result == GuiButtonTypeCenter) {
        view_dispatcher_send_custom_event(app->view_dispatcher, TPMSSceneRelearnEventTrigger);
    }
}

void tpms_scene_relearn_config_on_enter(void* context) {
    TPMSApp* app = context;

    widget_reset(app->widget);

    widget_add_string_multiline_element(
        app->widget, 64, 4, AlignCenter, AlignTop, FontPrimary, "Trigger TPMS");

    widget_add_text_scroll_element(
        app->widget,
        0,
        18,
        128,
        29,
        "\ecHold the Flippers back flat\n"
        "\ecagainst the base of the valve\n"
        "\ecstem & Press TRIGGER to send\n"
        "\eca 125kHz wake/activation pulse.");
    widget_add_button_element(
        app->widget, GuiButtonTypeCenter, "Trigger", tpms_scene_relearn_widget_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, TPMSViewWidget);
}

bool tpms_scene_relearn_config_on_event(void* context, SceneManagerEvent event) {
    TPMSApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == TPMSSceneRelearnEventTrigger) {
            tpms_relearn_lf_start(app);
            notification_message(app->notifications, &sequence_blink_green_10);
            consumed = true;
        }
    }

    return consumed;
}

void tpms_scene_relearn_config_on_exit(void* context) {
    TPMSApp* app = context;
    if(app->lf_relearn_active) {
        tpms_relearn_lf_stop(app);
    }
    widget_reset(app->widget);
}
