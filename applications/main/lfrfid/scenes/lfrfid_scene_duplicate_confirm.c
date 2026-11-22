#include "../lfrfid_i.h"

void lfrfid_scene_duplicate_confirm_on_enter(void* context) {
    LfRfid* app = context;
    Widget* widget = app->widget;

    FuriString* display_text = furi_string_alloc_printf(
        "\e#Duplicate Tag Detected!\e#\n"
        "Already saved as:\n"
        "%s",
        furi_string_get_cstr(app->duplicate_name));

    widget_add_text_box_element(
        widget, 0, 0, 128, 52, AlignLeft, AlignTop, furi_string_get_cstr(display_text), false);
    widget_add_button_element(widget, GuiButtonTypeLeft, "Cancel", lfrfid_widget_callback, app);
    widget_add_button_element(widget, GuiButtonTypeRight, "Save", lfrfid_widget_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, LfRfidViewWidget);
    furi_string_free(display_text);
}

bool lfrfid_scene_duplicate_confirm_on_event(void* context, SceneManagerEvent event) {
    LfRfid* app = context;
    SceneManager* scene_manager = app->scene_manager;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeBack) {
        consumed = true;
        scene_manager_previous_scene(scene_manager);
    } else if(event.type == SceneManagerEventTypeCustom) {
        consumed = true;
        if(event.event == GuiButtonTypeLeft) {
            scene_manager_previous_scene(scene_manager);
        } else if(event.event == GuiButtonTypeRight) {
            scene_manager_next_scene(scene_manager, LfRfidSceneSaveName);
        }
    }

    return consumed;
}

void lfrfid_scene_duplicate_confirm_on_exit(void* context) {
    LfRfid* app = context;
    widget_reset(app->widget);
}
