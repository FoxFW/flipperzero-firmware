#include <furi.h>
#include <furi/core/string.h>
#include <core/log.h>
#include <core/string.h>
#include "saflok_app_i.h"
#include <gui/modules/submenu.h>
#include <gui/view_dispatcher.h>
#include "../dialogs/saflok_dialogs.h"

void toplevel_callback(void* context, uint32_t index) {
    furi_assert(context);
    SaflokApp* app = context;

    switch(index) {
        case 0:
            scene_manager_next_scene(app->scene_manager, SaflokSceneFieldSelect);
            break;
        case 1:
            scene_manager_next_scene(app->scene_manager, SaflokScenePresetKeySelect);
            break;
        case 2:
            scene_manager_next_scene(app->scene_manager, SaflokSceneEmulate);
            break;
        case 3:
            if (app->nfc_mode) {
                scene_manager_next_scene(app->scene_manager, SaflokSceneMfClassicWriteInitial);
            } else {
                scene_manager_next_scene(app->scene_manager, SaflokSceneWriteToTag);
            }
            break;
        case 4:
            scene_manager_next_scene(app->scene_manager, SaflokSceneDeleteTag);
            break;
        case 5:
            scene_manager_next_scene(app->scene_manager, SaflokSceneViewInfo);
            break;
        case 6:
            scene_manager_next_scene(app->scene_manager, SaflokSceneDisplaySettings);
            break;
        case 7:
            app->nfc_mode = !app->nfc_mode;

            popup_reset(app->popup);
            popup_set_header(app->popup, app->nfc_mode ? 
                    "NFC Mode Enabled" : "File Mode Enabled", 
                    64, 16, AlignCenter, AlignCenter);
            popup_set_text(app->popup, app->nfc_mode ? 
                    "Ready to read/write cards" : "Using file operations", 
                    64, 32, AlignCenter, AlignCenter);
            popup_set_timeout(app->popup, 1500);
            view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewPopup);
            furi_delay_ms(1500);

            view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventReturnFromToggleNfcMode);
            scene_manager_next_scene(app->scene_manager, SaflokSceneTopLevel);
            break;
        default:
            break;
    }
}

void saflok_scene_top_level_on_enter(void* context) {
    furi_assert(context);
    SaflokApp* app = context;
    Submenu* topmenu = app->topmenu;

    submenu_reset(topmenu);
    submenu_set_header(topmenu, "       Saflok Editor");
    submenu_add_item(topmenu, "Modify Fields", 0, toplevel_callback, app);
    submenu_add_item(topmenu, "Generate Preset Key", 1, toplevel_callback, app);
    submenu_add_item(topmenu, "Emulate Tag", 2, toplevel_callback, app);
    submenu_add_item(topmenu, "Write to Tag", 3, toplevel_callback, app);
    submenu_add_item(topmenu, "Delete Tag", 4, toplevel_callback, app);
    submenu_add_item(topmenu, "View Tag Info", 5, toplevel_callback, app);
    submenu_add_item(topmenu, "Display Settings", 6, toplevel_callback, app);
    submenu_add_item(topmenu, app->nfc_mode ? "Switch to File Mode" : "Switch to NFC Mode", 7, toplevel_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewTopMenu);
}

bool saflok_scene_top_level_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;

    switch(event.type) {
        case SceneManagerEventTypeCustom: {
            if(event.event == SaflokCustomEventReturnFromPresetKey) {
                submenu_set_selected_item(app->topmenu, 1);
                return true;
            } else if(event.event == SaflokCustomEventReturnFromEmulate) {
                submenu_set_selected_item(app->topmenu, 2);
                return true;
            } else if(event.event == SaflokCustomEventReturnFromWriteToTag) {
                submenu_set_selected_item(app->topmenu, 3);
                return true;
            } else if(event.event == SaflokCustomEventReturnFromDeleteTag) {
                submenu_set_selected_item(app->topmenu, 4);
                return true;
            } else if(event.event == SaflokCustomEventReturnFromViewInfo) {
                submenu_set_selected_item(app->topmenu, 5);
                return true;
            } else if(event.event == SaflokCustomEventReturnFromDisplaySettings) {
                submenu_set_selected_item(app->topmenu, 6);
                return true;
            } else if(event.event == SaflokCustomEventReturnFromToggleNfcMode) {
                submenu_set_selected_item(app->topmenu, 7);
                return true;
            }
            break;
        }
        case SceneManagerEventTypeBack: {
            if(has_unsaved_changes(app)) {
                SaveResponse response = saflok_dialog_unsaved_changes(app);
                
                if(response == SaveResponseSave) {
                    save_changes(app);
                    if(app->nfc_mode) {
                        scene_manager_next_scene(app->scene_manager, SaflokSceneMfClassicWriteInitial);
                    } else {
                        scene_manager_next_scene(app->scene_manager, SaflokSceneWriteToTag);
                    }
                    return true;
                } else if(response == SaveResponseCancel) {
                    return true;
                }
            }
            
            FURI_LOG_D("SaflokApp", "Back pressed in TopLevel scene");
            FURI_LOG_D("SaflokApp", "Has Start scene: %d", scene_manager_has_previous_scene(app->scene_manager, SaflokSceneStart));

            if(scene_manager_has_previous_scene(app->scene_manager, SaflokSceneStart)) {
                scene_manager_search_and_switch_to_previous_scene(app->scene_manager, SaflokSceneStart);
            } else {
                scene_manager_stop(app->scene_manager);
                view_dispatcher_stop(app->view_dispatcher);
            }
            return true;
        }
        default:
            break;
    }

    return false;
}

void saflok_scene_top_level_on_exit(void* context) {
    furi_assert(context);
    SaflokApp* app = context;
    submenu_reset(app->topmenu);
}
