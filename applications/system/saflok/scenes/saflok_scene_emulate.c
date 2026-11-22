#include "../saflok_app_i.h"

#include "../helpers/protocol_support/saflok_protocol_support_defs.h"
#include "../helpers/protocol_support/saflok_protocol_support_gui_common.h"


#undef TAG
#define TAG "SaflokSceneEmulate"

FuriTimer* timer_auto_exit = NULL;

enum {
    SaflokSceneEmulateStateWidget,
    SaflokSceneEmulateStateWidgetLog,
    SaflokSceneEmulateStateTextBox,
};

void saflok_scene_emulate_timer_callback(void* context) {
    SaflokApp* app = context;

    view_dispatcher_send_custom_event(
        app->view_dispatcher, SaflokCustomEventEmulationTimeExpired);
}

void saflok_scene_emulate_widget_callback(GuiButtonType result, InputType type, void* context) {
    furi_assert(context);
    SaflokApp* app = context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, result);
    }
}

void saflok_scene_emulate_on_enter(void* context) {
    FURI_LOG_D(TAG, "on_enter");
    furi_assert(context);

    SaflokApp* app = context;
    Widget* widget = app->widget;
    TextBox* text_box = app->text_box;

    FuriString* temp_str = furi_string_alloc();

    widget_add_icon_element(widget, 0, 0, &I_NFC_dolphin_emulation_51x64);

    widget_add_string_element(widget, 90, 26, AlignCenter, AlignCenter, FontPrimary, "Emulating");
    if(!furi_string_empty(app->file_name)) {
        furi_string_printf(temp_str, "%s\n%s", nfc_device_get_name(app->saflok_device, NfcDeviceNameTypeFull), furi_string_get_cstr(app->file_name));
    } else {
        furi_string_printf(temp_str, "Unsaved\n%s", nfc_device_get_name(app->saflok_device, NfcDeviceNameTypeFull));
        furi_string_replace_str(temp_str, "Mifare", "MIFARE");
    }

    widget_add_text_box_element(widget, 50, 33, 78, 31, AlignCenter, AlignTop, furi_string_get_cstr(temp_str), false);

    furi_string_free(temp_str);

    text_box_set_font(text_box, TextBoxFontHex);
    text_box_set_focus(text_box, TextBoxFocusEnd);
    furi_string_reset(app->text_box_store);

    const MfClassicData* data = nfc_device_get_data(app->saflok_device, NfcProtocolMfClassic);
    app->listener = nfc_listener_alloc(app->nfc, NfcProtocolMfClassic, data);
    nfc_listener_start(app->listener, NULL, NULL);

    scene_manager_set_scene_state(app->scene_manager, SaflokSceneEmulate, SaflokSceneEmulateStateWidget);

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewWidget);
    saflok_blink_emulate_start(app);

    if(app->fav_timeout) {
        timer_auto_exit = furi_timer_alloc(saflok_scene_emulate_timer_callback, FuriTimerTypeOnce, app);
        furi_timer_start(timer_auto_exit, 30 /* was momentum_settings.favorite_timeout; Fox has no Momentum favorites */ * furi_kernel_get_tick_frequency());
    }
}

bool saflok_scene_emulate_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
 
    FURI_LOG_D(TAG, "on_event");
    
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SaflokCustomEventEmulationTimeExpired) {
            if(!scene_manager_previous_scene(app->scene_manager)) {
                scene_manager_stop(app->scene_manager);
                view_dispatcher_stop(app->view_dispatcher);
            } else {
                scene_manager_previous_scene(app->scene_manager);
            }
            return true;
        }
    }

    furi_assert(context);

    bool consumed = false;

    const uint32_t state = scene_manager_get_scene_state(app->scene_manager, SaflokSceneEmulate);

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SaflokCustomEventListenerUpdate) {
            if(furi_string_size(app->text_box_store)) {
                widget_add_button_element(app->widget, GuiButtonTypeCenter, "Log", saflok_scene_emulate_widget_callback, app);
                scene_manager_set_scene_state(app->scene_manager, SaflokSceneEmulate, SaflokSceneEmulateStateWidgetLog);
            }
            text_box_set_text(app->text_box, furi_string_get_cstr(app->text_box_store));
            consumed = true;
        } else if(event.event == GuiButtonTypeCenter) {
            if(state == SaflokSceneEmulateStateWidgetLog) {
                view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewTextBox);
                scene_manager_set_scene_state(app->scene_manager, SaflokSceneEmulate, SaflokSceneEmulateStateTextBox);
                consumed = true;
            }
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        if(state == SaflokSceneEmulateStateTextBox) {
            view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewWidget);
            scene_manager_set_scene_state(app->scene_manager, SaflokSceneEmulate, SaflokSceneEmulateStateWidgetLog);
            consumed = true;
        }
    }

    return consumed;
}

void saflok_scene_emulate_on_exit(void* context) {
    FURI_LOG_D(TAG, "on_exit");
    
    if(timer_auto_exit) {
        furi_timer_stop(timer_auto_exit);
        furi_timer_free(timer_auto_exit);
        timer_auto_exit = NULL;
    }
    
    furi_assert(context);

    SaflokApp* app = context;
    nfc_listener_stop(app->listener);

    const NfcProtocol protocol = nfc_device_get_protocol(app->saflok_device);

    if(protocol == nfc_listener_get_protocol(app->listener)) {
        const NfcDeviceData* data = nfc_listener_get_data(app->listener, protocol);

        if(!nfc_device_is_equal_data(app->saflok_device, protocol, data)) {
            nfc_device_set_data(app->saflok_device, protocol, data);
            saflok_save_shadow_file(app);
        }
    }

    nfc_listener_free(app->listener);

    widget_reset(app->widget);
    text_box_reset(app->text_box);
    furi_string_reset(app->text_box_store);

    saflok_blink_stop(app);
}
