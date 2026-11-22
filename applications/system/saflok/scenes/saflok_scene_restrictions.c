#include "saflok_app_i.h"
#include "views/option_list.h"
#include "helpers/saflok_app_helpers.h"
#include "helpers/saflok_field_helpers.h"
#include <furi_hal.h>

#undef TAG
#define TAG "SaflokSceneRestrictions"

static OptionEntry* updating_entries[16] = {NULL};
static uint8_t updating_count = 0;

static bool is_entry_updating(OptionEntry* entry) {
    for(uint8_t i = 0; i < updating_count; i++) {
        if(updating_entries[i] == entry) {
            return true;
        }
    }
    return false;
}

static void add_updating_entry(OptionEntry* entry) {
    if(updating_count < 16) {
        updating_entries[updating_count++] = entry;
    }
}

static void remove_updating_entry(OptionEntry* entry) {
    for(uint8_t i = 0; i < updating_count; i++) {
        if(updating_entries[i] == entry) {
            for(uint8_t j = i; j < updating_count - 1; j++) {
                updating_entries[j] = updating_entries[j + 1];
            }
            updating_count--;
            break;
        }
    }
}

static void restriction_change_callback(OptionEntry* entry) {
    SaflokApp* app = option_entry_get_context(entry);
    if(!app) return;
    
    if(is_entry_updating(entry)) return;
    
    add_updating_entry(entry);
    
    uint8_t index = entry->index;
    
    bool enabled = (option_entry_get_current_int_value(entry) == 1);
    
    FURI_LOG_I(TAG, "%s changed to: %s", 
              app->saflok_data->time_control.restrictions.day_array[index].full_name, 
              enabled ? "ON" : "OFF");
    
    if(index == ALL) {
        if(enabled) {
            for(int i = MONDAY; i < NONE; i++) {
                app->saflok_data->time_control.restrictions.day_array[i].enabled = true;
            }
        } else {
            for(int i = MONDAY; i < NONE; i++) {
                app->saflok_data->time_control.restrictions.day_array[i].enabled = app->temp_storage.saved_restrictions[i];
            }
        }
    } else if(index == WEEKDAYS) {
        if(enabled) {
            for(int i = MONDAY; i <= FRIDAY; i++) {
                app->saflok_data->time_control.restrictions.day_array[i].enabled = true;
            }
            
            if(app->saflok_data->time_control.restrictions.day_array[WEEKENDS].enabled) {
                app->saflok_data->time_control.restrictions.day_array[ALL].enabled = true;
            }
        } else {
            app->saflok_data->time_control.restrictions.day_array[ALL].enabled = false;
            
            for(int i = MONDAY; i <= FRIDAY; i++) {
                app->saflok_data->time_control.restrictions.day_array[i].enabled = app->temp_storage.saved_restrictions[i];
            }
        }
    } else if(index == WEEKENDS) {
        if(enabled) {
            for(int i = SATURDAY; i <= SUNDAY; i++) {
                app->saflok_data->time_control.restrictions.day_array[i].enabled = true;
            }
            
            if(app->saflok_data->time_control.restrictions.day_array[WEEKDAYS].enabled) {
                app->saflok_data->time_control.restrictions.day_array[ALL].enabled = true;
            }
        } else {
            app->saflok_data->time_control.restrictions.day_array[ALL].enabled = false;
            
            for(int i = SATURDAY; i <= SUNDAY; i++) {
                app->saflok_data->time_control.restrictions.day_array[i].enabled = app->temp_storage.saved_restrictions[i];
            }
        }
    } else if(index <= FRIDAY) {
        app->saflok_data->time_control.restrictions.day_array[index].enabled = enabled;
        
        if(enabled) {
            bool all_weekdays_enabled = true;
            for(int i = MONDAY; i <= FRIDAY; i++) {
                if(!app->saflok_data->time_control.restrictions.day_array[i].enabled) {
                    all_weekdays_enabled = false;
                    break;
                }
            }
            
            app->saflok_data->time_control.restrictions.day_array[WEEKDAYS].enabled = all_weekdays_enabled;
            
            if(all_weekdays_enabled && app->saflok_data->time_control.restrictions.day_array[WEEKENDS].enabled) {
                app->saflok_data->time_control.restrictions.day_array[ALL].enabled = true;
            }
        } else {
            app->saflok_data->time_control.restrictions.day_array[WEEKDAYS].enabled = false;
            app->saflok_data->time_control.restrictions.day_array[ALL].enabled = false;
        }
    } else if(index >= SATURDAY && index <= SUNDAY) {
        app->saflok_data->time_control.restrictions.day_array[index].enabled = enabled;
        
        if(enabled) {
            bool all_weekend_enabled = true;
            for(int i = SATURDAY; i <= SUNDAY; i++) {
                if(!app->saflok_data->time_control.restrictions.day_array[i].enabled) {
                    all_weekend_enabled = false;
                    break;
                }
            }
            
            app->saflok_data->time_control.restrictions.day_array[WEEKENDS].enabled = all_weekend_enabled;
            
            if(all_weekend_enabled && app->saflok_data->time_control.restrictions.day_array[WEEKDAYS].enabled) {
                app->saflok_data->time_control.restrictions.day_array[ALL].enabled = true;
            }
        } else {
            app->saflok_data->time_control.restrictions.day_array[WEEKENDS].enabled = false;
            app->saflok_data->time_control.restrictions.day_array[ALL].enabled = false;
        }
    } else if(index == NONE) {
        app->saflok_data->time_control.restrictions.day_array[NONE].enabled = enabled;
        
        if(enabled) {
            for(int i = MONDAY; i < NONE; i++) {
                app->saflok_data->time_control.restrictions.day_array[i].enabled = false;
            }
        } else {
            bool weekdays_enabled = true;
            bool weekends_enabled = true;
            
            for(int i = MONDAY; i <= FRIDAY; i++) {
                bool enable = app->temp_storage.saved_restrictions[i];
                app->saflok_data->time_control.restrictions.day_array[i].enabled = enable;
                if(!enable) {
                    weekdays_enabled = false;
                }
            }
            
            for(int i = SATURDAY; i <= SUNDAY; i++) {
                bool enable = app->temp_storage.saved_restrictions[i];
                app->saflok_data->time_control.restrictions.day_array[i].enabled = enable;
                if(!enable) {
                    weekends_enabled = false;
                }
            }
            
            app->saflok_data->time_control.restrictions.day_array[WEEKDAYS].enabled = weekdays_enabled;
            app->saflok_data->time_control.restrictions.day_array[WEEKENDS].enabled = weekends_enabled;
            app->saflok_data->time_control.restrictions.day_array[ALL].enabled = weekdays_enabled && weekends_enabled;
        }
    }
    
    for(int i = MONDAY; i < DAY_COUNT; i++) {
        if(i == index) continue;
        
        bool should_be_enabled = app->saflok_data->time_control.restrictions.day_array[i].enabled;
        OptionEntry* day_entry = option_list_get_entry_by_index(app->restrictions_list, i);
        
        if(day_entry && !is_entry_updating(day_entry)) {
            int current_value = option_entry_get_current_int_value(day_entry);
            bool is_enabled = (current_value == 1);
            
            if(is_enabled != should_be_enabled) {
                add_updating_entry(day_entry);
                option_entry_set_current_index(day_entry, should_be_enabled ? 1 : 0);
                remove_updating_entry(day_entry);
            }
        }
    }
    
    app->changed->restrictions = true;
    
    remove_updating_entry(entry);
}

void saflok_scene_restrictions_on_enter(void* context) {
    furi_assert(context);
    SaflokApp* app = context;
    
    updating_count = 0;
    memset(updating_entries, 0, sizeof(updating_entries));
    OptionList* list = app->restrictions_list;
    option_list_reset(list);
    

    option_list_set_header(list, "Select Restricted Days");
    
    for(int i = 0; i < DAY_COUNT; i++) {
        app->temp_storage.saved_restrictions[i] = app->saflok_data->time_control.restrictions.day_array[i].enabled;
    }
    
    for(uint8_t i = 0; i < DAY_COUNT; i++) {
        OptionEntry* entry = option_entry_alloc(
            app->saflok_data->time_control.restrictions.day_array[i].full_name,
            i,
            restriction_change_callback,
            app);
        
        option_entry_add_int_value(entry, "OFF", 0, true);
        option_entry_add_int_value(entry, "ON", 1, true);
        
        bool restriction_enabled = app->saflok_data->time_control.restrictions.day_array[i].enabled;
        option_entry_set_current_index(entry, restriction_enabled ? 1 : 0);
        
        option_list_add_entry(app->restrictions_list, entry);
    }
    
    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewRestrictionsList);
}

bool saflok_scene_restrictions_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    bool consumed = false;
    
    if(event.type == SceneManagerEventTypeBack) {
        consumed = scene_manager_previous_scene(app->scene_manager);
    }
    
    return consumed;
}

void saflok_scene_restrictions_on_exit(void* context) {
    FURI_LOG_I(TAG, "Exiting restrictions scene");
    SaflokApp* app = context;
    if(!app) return;
    
    FURI_LOG_D("MemoryDebug", "Restrictions exit - before cleanup. Free heap: %u bytes", memmgr_get_free_heap());
    
    
    for(uint8_t i = 0; i < DAY_COUNT; i++) {
        OptionEntry* entry = option_list_get_entry_by_index(app->restrictions_list, i);
        if(entry) {
            app->saflok_data->time_control.restrictions.day_array[i].enabled = 
                (option_entry_get_current_int_value(entry) == 1);
        }
    }
    
    if(app->changed->restrictions) {
        saflok_pack_current_restrictions(app);
    }
    
    
    FURI_LOG_D("MemoryDebug", "Restrictions exit - after cleanup. Free heap: %u bytes", memmgr_get_free_heap());
}
