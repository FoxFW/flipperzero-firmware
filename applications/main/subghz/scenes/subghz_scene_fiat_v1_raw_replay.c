#include "../subghz_i.h"

static void subghz_scene_fiat_v1_raw_replay_widget_callback(
    GuiButtonType result,
    InputType type,
    void* context) {
    SubGhz* subghz = context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(subghz->view_dispatcher, result);
    }
}

void subghz_scene_fiat_v1_raw_replay_on_enter(void* context) {
    SubGhz* subghz = context;

    widget_reset(subghz->widget);
    widget_add_text_box_element(
        subghz->widget,
        0,
        0,
        128,
        46,
        AlignCenter,
        AlignTop,
        "\e#Send Raw?\e#\nRaw replay sends the raw "
        "code. It will not roll, so "
        "it will not\nopen the car.",
        true);
    widget_add_button_element(
        subghz->widget,
        GuiButtonTypeCenter,
        "Send",
        subghz_scene_fiat_v1_raw_replay_widget_callback,
        subghz);

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdWidget);
}

bool subghz_scene_fiat_v1_raw_replay_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == GuiButtonTypeCenter) {
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneTransmitter);
            return true;
        }
    }
    return false;
}

void subghz_scene_fiat_v1_raw_replay_on_exit(void* context) {
    SubGhz* subghz = context;
    widget_reset(subghz->widget);
}
