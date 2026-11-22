#include <furi.h>
#include <furi/core/string.h>
#include <core/log.h>
#include <core/string.h>
#include "saflok_app_i.h"
#include <gui/modules/submenu.h>
#include <gui/view_dispatcher.h>
#include "../helpers/saflok_field_helpers.h"
#include "../helpers/saflok_app_helpers.h"
#include "../dialogs/saflok_dialogs.h"

extern void write_to_file(SaflokApp* app);

void preset_key_select_helper(SaflokApp* app) {
    app->edit_original = true;
    saflok_update_nfc_device(app);
    scene_manager_next_scene(app->scene_manager, SaflokSceneTopLevel);
}

void preset_key_select_callback(void* context, uint32_t index) {
    furi_assert(context);
    SaflokApp* app = context;

    switch(index) {
        case 0:
            saflok_create_battery_disconnect_key(app);
            preset_key_select_helper(app);
            break;
        case 1:
            saflok_create_clock_date_key(app);
            preset_key_select_helper(app);
            break;
        case 2:
            saflok_create_clock_time_key(app);
            preset_key_select_helper(app);
            break;
        case 3:
            saflok_create_deadbolt_switch_status_key(app);
            preset_key_select_helper(app);
            break;
        case 4:
            saflok_create_disable_latch_key(app);
            preset_key_select_helper(app);
            break;
        case 5:
            saflok_create_e2_changes_key(app);
            preset_key_select_helper(app);
            break;
        case 6:
            saflok_create_emergency_key(app);
            preset_key_select_helper(app);
            break;
        case 7:
            saflok_create_enable_latch_key(app);
            preset_key_select_helper(app);
            break;
        case 8:
            saflok_create_eprom_version_key(app);
            preset_key_select_helper(app);
            break;
        case 9:
            saflok_create_erase_lock_memory_key(app);
            preset_key_select_helper(app);
            break;
        case 10:
            saflok_create_interrogation_key(app);
            preset_key_select_helper(app);
            break;
        case 11:
            saflok_create_knob_switch_status_key(app);
            preset_key_select_helper(app);
            break;
        case 12:
            saflok_create_led_diagnostics_key(app);
            preset_key_select_helper(app);
            break;
        case 13:
            saflok_create_low_battery_status_key(app);
            preset_key_select_helper(app);
            break;
        case 14:
            saflok_create_primary_program_key(app);
            preset_key_select_helper(app);
            break;
        case 15:
            saflok_create_resequence_key(app);
            preset_key_select_helper(app);
            break;
        case 16:
            saflok_create_secondary_program_key(app);
            preset_key_select_helper(app);
            break;
        default:
            break;
    }
}

void saflok_scene_preset_key_select_on_enter(void* context) {
    furi_assert(context);
    SaflokApp* app = context;
    Submenu* preset_key_menu = app->preset_key_select_menu;

    submenu_reset(preset_key_menu);
    submenu_set_header(preset_key_menu, "     Select Key Type");
    submenu_add_item(preset_key_menu, "Battery Disconnect Key", 0, preset_key_select_callback, app);
    submenu_add_item(preset_key_menu, "Clock Date Key", 1, preset_key_select_callback, app);
    submenu_add_item(preset_key_menu, "Clock Time Key", 2, preset_key_select_callback, app);
    submenu_add_item(preset_key_menu, "Deadbolt Status Key", 3, preset_key_select_callback, app);
    submenu_add_item(preset_key_menu, "Disable Latch Key", 4, preset_key_select_callback, app);
    submenu_add_item(preset_key_menu, "E2 Changes Key", 5, preset_key_select_callback, app);
    submenu_add_item(preset_key_menu, "Emergency Key", 6, preset_key_select_callback, app);
    submenu_add_item(preset_key_menu, "Enable Latch Key", 7, preset_key_select_callback, app);
    submenu_add_item(preset_key_menu, "EPROM Version Key", 8, preset_key_select_callback, app);
    submenu_add_item(preset_key_menu, "Erase Lock Memory Key", 9, preset_key_select_callback, app);
    submenu_add_item(preset_key_menu, "Interrogation Key", 10, preset_key_select_callback, app);
    submenu_add_item(preset_key_menu, "Knob Status Key", 11, preset_key_select_callback, app);
    submenu_add_item(preset_key_menu, "LED Diagnostics Key", 12, preset_key_select_callback, app);
    submenu_add_item(preset_key_menu, "Low Battery Status Key", 13, preset_key_select_callback, app);
    submenu_add_item(preset_key_menu, "Primary Program Key", 14, preset_key_select_callback, app);
    submenu_add_item(preset_key_menu, "Resequence Key", 15, preset_key_select_callback, app);
    submenu_add_item(preset_key_menu, "Secondary Program Key", 16, preset_key_select_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewPresetKeySelect);
}

bool saflok_scene_preset_key_select_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;

    switch(event.type) {
        case SceneManagerEventTypeBack: {
            if(scene_manager_has_previous_scene(app->scene_manager, SaflokSceneTopLevel)) {
                view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventReturnFromPresetKey);
                scene_manager_search_and_switch_to_previous_scene(app->scene_manager, SaflokSceneTopLevel);
            }
            return true;
        }
        default:
            break;
    }

    return false;
}

void saflok_scene_preset_key_select_on_exit(void* context) {
    furi_assert(context);
    SaflokApp* app = context;
    submenu_reset(app->preset_key_select_menu);
}
