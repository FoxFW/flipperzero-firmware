#include "../saflok_app_i.h"
#include "../views/option_list.h"

#undef TAG
#define TAG "SaflokSceneDisplaySettings"

static void key_id_format_changed_callback(OptionEntry* entry) {
    SaflokApp* app = option_entry_get_context(entry);
    
    app->display_settings->key_id_hex_format = (option_entry_get_current_int_value(entry) == 1);
}

static void key_record_format_changed_callback(OptionEntry* entry) {
    SaflokApp* app = option_entry_get_context(entry);
    
    app->display_settings->key_record_hex_format = (option_entry_get_current_int_value(entry) == 1);
}

static void property_number_format_changed_callback(OptionEntry* entry) {
    SaflokApp* app = option_entry_get_context(entry);
    
    app->display_settings->property_number_hex_format = (option_entry_get_current_int_value(entry) == 1);
}

static void sequence_combo_format_changed_callback(OptionEntry* entry) {
    SaflokApp* app = option_entry_get_context(entry);
    
    app->display_settings->sequence_combo_hex_format = (option_entry_get_current_int_value(entry) == 1);
}

static void advanced_fields_callback(OptionEntry* entry) {
    SaflokApp* app = option_entry_get_context(entry);
    
    app->display_settings->show_advanced_fields = (option_entry_get_current_int_value(entry) == 1);
}

void saflok_scene_display_settings_on_enter(void* context) {
    SaflokApp* app = context;
    OptionList* list = app->display_settings_list;

    option_list_set_header(list, "DisplaySettings");
    option_list_set_subheader(list, "Format");

    OptionEntry* key_id_format_item = option_entry_alloc(
        "Key ID",
        0,
        key_id_format_changed_callback,
        app);
    
    option_entry_add_int_value(key_id_format_item, "Int", 0, true);
    option_entry_add_int_value(key_id_format_item, "Hex", 1, true);
    
    option_entry_set_current_index(key_id_format_item, app->display_settings->key_id_hex_format ? 1 : 0);
    
    option_list_add_entry(list, key_id_format_item);

    OptionEntry* key_record_format_item = option_entry_alloc(
        "Key Record",
        1,
        key_record_format_changed_callback,
        app);
    
    option_entry_add_int_value(key_record_format_item, "Int", 0, true);
    option_entry_add_int_value(key_record_format_item, "Hex", 1, true);
    option_entry_set_current_index(key_record_format_item, app->display_settings->key_record_hex_format ? 1 : 0);
    option_list_add_entry(list, key_record_format_item);

    OptionEntry* property_number_format_item = option_entry_alloc(
        "Property Number",
        2,
        property_number_format_changed_callback,
        app);
    
    option_entry_add_int_value(property_number_format_item, "Int", 0, true);
    option_entry_add_int_value(property_number_format_item, "Hex", 1, true);
    option_entry_set_current_index(
        property_number_format_item, 
        app->display_settings->property_number_hex_format ? 1 : 0);
    option_list_add_entry(list, property_number_format_item);

    OptionEntry* sequence_combo_format_item = option_entry_alloc(
        "Sequence Combo",
        3,
        sequence_combo_format_changed_callback,
        app);
    
    option_entry_add_int_value(sequence_combo_format_item, "Int", 0, true);
    option_entry_add_int_value(sequence_combo_format_item, "Hex", 1, true);
    option_entry_set_current_index(
        sequence_combo_format_item, 
        app->display_settings->sequence_combo_hex_format ? 1 : 0);
    option_list_add_entry(list, sequence_combo_format_item);

    OptionEntry* advanced_item = option_entry_alloc(
        "Advanced Fields",
        4,
        advanced_fields_callback,
        app);
    
    option_entry_add_int_value(advanced_item, "Hide", 0, true);
    option_entry_add_int_value(advanced_item, "Show", 1, true);
    option_entry_set_current_index(
        advanced_item, 
        app->display_settings->show_advanced_fields ? 1 : 0);
    option_list_add_entry(list, advanced_item);

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewDisplaySettingsList);
}

bool saflok_scene_display_settings_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    
    switch(event.type) {
        case SceneManagerEventTypeBack: {
            if(scene_manager_has_previous_scene(app->scene_manager, SaflokSceneTopLevel)) {
                view_dispatcher_send_custom_event(app->view_dispatcher, SaflokCustomEventReturnFromDisplaySettings);
                scene_manager_search_and_switch_to_previous_scene(app->scene_manager, SaflokSceneTopLevel);
            }
            return true;
        }
        default:
            break;
    }

    return false;
}

void saflok_scene_display_settings_on_exit(void* context) {
    UNUSED(context);
}
