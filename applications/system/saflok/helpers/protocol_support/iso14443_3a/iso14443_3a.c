#include "iso14443_3a.h"
#include "iso14443_3a_render.h"

#include <nfc/protocols/iso14443_3a/iso14443_3a_poller.h>

#include "../../../saflok_app_i.h"

#include "../saflok_protocol_support_common.h"
#include "../saflok_protocol_support_gui_common.h"

static void saflok_scene_info_on_enter_iso14443_3a(SaflokApp* app) {
    const NfcDevice* device = app->saflok_device;
    const Iso14443_3aData* data = nfc_device_get_data(device, NfcProtocolIso14443_3a);

    FuriString* temp_str = furi_string_alloc();
    saflok_append_filename_string_when_present(app, temp_str);

    furi_string_cat_printf(
        temp_str, "\e#%s\n", nfc_device_get_name(device, NfcDeviceNameTypeFull));
    saflok_render_iso14443_3a_info(data, SaflokProtocolFormatTypeFull, temp_str);

    widget_add_text_scroll_element(
        app->widget, 0, 0, 128, 64, furi_string_get_cstr(temp_str));

    furi_string_free(temp_str);
}

static NfcCommand
    saflok_scene_read_poller_callback_iso14443_3a(NfcGenericEvent event, void* context) {
    furi_assert(event.protocol == NfcProtocolIso14443_3a);

    SaflokApp* app = context;
    const Iso14443_3aPollerEvent* iso14443_3a_event = event.event_data;

    if(iso14443_3a_event->type == Iso14443_3aPollerEventTypeReady) {
        nfc_device_set_data(
            app->saflok_device, NfcProtocolIso14443_3a, nfc_poller_get_data(app->poller));
        view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventPollerSuccess);
        return NfcCommandStop;
    }

    return NfcCommandContinue;
}

static void saflok_scene_read_on_enter_iso14443_3a(SaflokApp* app) {
    nfc_poller_start(app->poller, saflok_scene_read_poller_callback_iso14443_3a, app);
}

static void saflok_scene_read_success_on_enter_iso14443_3a(SaflokApp* app) {
    FURI_LOG_D("SaflokSceneReadSuccess", "saflok_scene_read_success_on_enter_iso14443_3a");
    const NfcDevice* device = app->saflok_device;
    const Iso14443_3aData* data = nfc_device_get_data(device, NfcProtocolIso14443_3a);

    FuriString* temp_str = furi_string_alloc();
    furi_string_cat_printf(temp_str, "\e#%s\n", nfc_device_get_name(device, NfcDeviceNameTypeFull));
    saflok_render_iso14443_3a_info(data, SaflokProtocolFormatTypeShort, temp_str);

    widget_add_text_scroll_element(
        app->widget, 0, 0, 128, 52, furi_string_get_cstr(temp_str));

    furi_string_free(temp_str);
}

static NfcCommand
    saflok_scene_emulate_listener_callback_iso14443_3a(NfcGenericEvent event, void* context) {
    furi_assert(context);
    furi_assert(event.protocol == NfcProtocolIso14443_3a);
    furi_assert(event.event_data);

    SaflokApp* nfc = context;
    Iso14443_3aListenerEvent* iso14443_3a_event = event.event_data;

    if(iso14443_3a_event->type == Iso14443_3aListenerEventTypeReceivedStandardFrame) {
        if(furi_string_size(nfc->text_box_store) < SAFLOK_LOG_SIZE_MAX) {
            furi_string_cat_printf(nfc->text_box_store, "R:");
            for(size_t i = 0; i < bit_buffer_get_size_bytes(iso14443_3a_event->data->buffer);
                i++) {
                furi_string_cat_printf(
                    nfc->text_box_store,
                    " %02X",
                    bit_buffer_get_byte(iso14443_3a_event->data->buffer, i));
            }
            furi_string_push_back(nfc->text_box_store, '\n');
            view_dispatcher_send_custom_event(nfc->view_dispatcher, SaflokCustomEventListenerUpdate);
        }
    }

    return NfcCommandContinue;
}

static void saflok_scene_emulate_on_enter_iso14443_3a(SaflokApp* app) {
    const Iso14443_3aData* data =
        nfc_device_get_data(app->saflok_device, NfcProtocolIso14443_3a);

    app->listener = nfc_listener_alloc(app->nfc, NfcProtocolIso14443_3a, data);
    nfc_listener_start(
        app->listener, saflok_scene_emulate_listener_callback_iso14443_3a, app);
}

static bool saflok_scene_read_menu_on_event_iso14443_3a(SaflokApp* app, SceneManagerEvent event) {
    if(event.type == SceneManagerEventTypeCustom && event.event == SubmenuIndexCommonEmulate) {
        scene_manager_next_scene(app->scene_manager, SaflokSceneEmulate);
        return true;
    }

    return false;
}

const SaflokProtocolSupportBase saflok_protocol_support_iso14443_3a = {
    .features = SaflokProtocolFeatureEmulateUid | SaflokProtocolFeatureEditUid,

    .scene_info =
        {
            .on_enter = saflok_scene_info_on_enter_iso14443_3a,
            .on_event = saflok_protocol_support_common_on_event_empty,
        },
    .scene_read =
        {
            .on_enter = saflok_scene_read_on_enter_iso14443_3a,
            .on_event = saflok_protocol_support_common_on_event_empty,
        },
    .scene_read_menu =
        {
            .on_enter = saflok_protocol_support_common_on_enter_empty,
            .on_event = saflok_scene_read_menu_on_event_iso14443_3a,
        },
    .scene_read_success =
        {
            .on_enter = saflok_scene_read_success_on_enter_iso14443_3a,
            .on_event = saflok_protocol_support_common_on_event_empty,
        },
    .scene_saved_menu =
        {
            .on_enter = saflok_protocol_support_common_on_enter_empty,
            .on_event = saflok_protocol_support_common_on_event_empty,
        },
    .scene_save_name =
        {
            .on_enter = saflok_protocol_support_common_on_enter_empty,
            .on_event = saflok_protocol_support_common_on_event_empty,
        },
    .scene_emulate =
        {
            .on_enter = saflok_scene_emulate_on_enter_iso14443_3a,
            .on_event = saflok_protocol_support_common_on_event_empty,
        },
};
