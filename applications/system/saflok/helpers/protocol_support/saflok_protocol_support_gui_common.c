#include "saflok_protocol_support_gui_common.h"

#include "../../saflok_app_i.h"

void saflok_protocol_support_common_submenu_callback(void* context, uint32_t index) {
    furi_assert(context);
    SaflokApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void saflok_protocol_support_common_widget_callback(
    GuiButtonType result,
    InputType type,
    void* context) {
    furi_assert(context);
    SaflokApp* app = context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, result);
    }
}

void saflok_protocol_support_common_byte_input_done_callback(void* context) {
    furi_assert(context);
    SaflokApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventByteInputDone);
}

void saflok_protocol_support_common_text_input_done_callback(void* context) {
    SaflokApp* app = context;

    view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventTextInputDone);
}

void saflok_protocol_support_common_on_enter_empty(SaflokApp* app) {
    UNUSED(app);
}

bool saflok_protocol_support_common_on_event_empty(SaflokApp* app, SceneManagerEvent event) {
    UNUSED(app);
    UNUSED(event);
    return event.type != SceneManagerEventTypeBack;
}
