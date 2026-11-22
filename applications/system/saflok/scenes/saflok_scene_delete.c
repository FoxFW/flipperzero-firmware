#include "../saflok_app_i.h"

#undef TAG
#define TAG "SaflokSceneDelete"

static void saflok_scene_delete_widget_callback(GuiButtonType result, InputType type, void* context) {
    SaflokApp* app = context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, result);
    }
}

void saflok_scene_delete_on_enter(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_enter");

    FuriString* temp_str;
    temp_str = furi_string_alloc();

    furi_string_printf(temp_str, "\e#Delete %s?\e#", furi_string_get_cstr(app->file_name));
    widget_add_text_box_element(app->widget, 0, 0, 128, 23, AlignCenter, AlignCenter, furi_string_get_cstr(temp_str), false);
    widget_add_button_element(app->widget, GuiButtonTypeLeft, "Cancel", saflok_scene_delete_widget_callback, app);
    widget_add_button_element(app->widget, GuiButtonTypeRight, "Delete", saflok_scene_delete_widget_callback, app);

    size_t uid_len;
    const uint8_t* uid = nfc_device_get_uid(app->saflok_device, &uid_len);

    furi_string_set(temp_str, "UID:");
    for(size_t i = 0; i < uid_len; i++) {
        furi_string_cat_printf(temp_str, " %02X", uid[i]);
    }
    widget_add_string_element(app->widget, 64, 24, AlignCenter, AlignTop, FontSecondary, furi_string_get_cstr(temp_str));

    furi_string_set_str(temp_str, nfc_device_get_name(app->saflok_device, NfcDeviceNameTypeFull));
    widget_add_string_element(app->widget, 64, 34, AlignCenter, AlignTop, FontSecondary, furi_string_get_cstr(temp_str));
    furi_string_free(temp_str);

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewWidget);
}

bool saflok_scene_delete_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    
    FURI_LOG_D(TAG, "on_event");
    
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == GuiButtonTypeLeft) {
            consumed = scene_manager_previous_scene(app->scene_manager);
        } else if(event.event == GuiButtonTypeRight) {
            if(saflok_delete(app)) {
                scene_manager_next_scene(app->scene_manager, SaflokSceneDeleteSuccess);
            } else {
                if(!scene_manager_search_and_switch_to_previous_scene(
                       app->scene_manager, SaflokSceneStart)) {
                    scene_manager_stop(app->scene_manager);
                    view_dispatcher_stop(app->view_dispatcher);
                }
            }
            consumed = true;
        }
    }
    return consumed;
}

void saflok_scene_delete_on_exit(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_exit");
    
    widget_reset(app->widget);
}
