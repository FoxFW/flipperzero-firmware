#include "../helpers/protocol_support/mf_classic/mf_classic.h"
#include "../helpers/protocol_support/mf_classic/mf_classic_render.h"

#include <nfc/protocols/mf_classic/mf_classic_poller.h>

#include "../saflok_app_i.h"

#include "../helpers/protocol_support/saflok_protocol_support_defs.h"
#include "../helpers/protocol_support/saflok_protocol_support_gui_common.h"

#undef TAG
#define TAG "SaflokSceneMoreInfo"

void saflok_scene_more_info_on_enter(void* context) {
    FURI_LOG_D(TAG, "on_enter");
    furi_assert(context);

    SaflokApp* app = context;
    const NfcDevice* device = app->saflok_device;
    SaflokMfClassicData* mfc_data = (SaflokMfClassicData*)nfc_device_get_data(device, NfcProtocolMfClassic);
    mfc_data->type = MfClassicType1k;

    furi_string_reset(app->text_box_store);
    saflok_render_mf_classic_dump((MfClassicData*)mfc_data, app->text_box_store);

    text_box_set_font(app->text_box, TextBoxFontHex);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text_box_store));
    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewTextBox);
}

bool saflok_scene_more_info_on_event(void* context, SceneManagerEvent event) { 
    FURI_LOG_D(TAG, "on_event");
    UNUSED(context);
    return event.type != SceneManagerEventTypeBack;
}

void saflok_scene_more_info_on_exit(void* context) {
    FURI_LOG_D(TAG, "on_exit");
    furi_assert(context);

    SaflokApp* app = context;
    
    text_box_reset(app->text_box);
    widget_reset(app->widget);
    furi_string_reset(app->text_box_store);
}
