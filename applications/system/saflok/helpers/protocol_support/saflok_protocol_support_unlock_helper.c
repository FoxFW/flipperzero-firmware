#include "saflok_protocol_support_unlock_helper.h"
#include "saflok_icons.h"

static void saflok_scene_read_setup_view(SaflokApp* app) {
    Popup* popup = app->popup;
    popup_reset(popup);
    uint32_t state = scene_manager_get_scene_state(app->scene_manager, SaflokSceneRead);

    if(state == SaflokSceneReadMenuStateCardSearch) {
        popup_set_icon(app->popup, 0, 8, &I_NFC_manual_60x50);
        popup_set_header(app->popup, "Unlocking", 97, 15, AlignCenter, AlignTop);
        popup_set_text(
            app->popup, "Hold card next\nto Flipper's back", 94, 27, AlignCenter, AlignTop);
    } else {
        popup_set_header(app->popup, "Don't move", 85, 27, AlignCenter, AlignTop);
        popup_set_icon(app->popup, 12, 20, &A_Loading_24);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewPopup);
}

void saflok_unlock_helper_setup_from_state(SaflokApp* app) {
    uint32_t state = SaflokSceneReadMenuStateCardFound;

    scene_manager_set_scene_state(app->scene_manager, SaflokSceneRead, state);

    saflok_scene_read_setup_view(app);
}

void saflok_unlock_helper_card_detected_handler(SaflokApp* app) {
    scene_manager_set_scene_state(
        app->scene_manager, SaflokSceneRead, SaflokSceneReadMenuStateCardFound);
    saflok_scene_read_setup_view(app);
}
