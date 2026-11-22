#include "saflok_app_i.h"
#include "views/option_list.h"
#include "helpers/saflok_app_helpers.h"
#include "helpers/saflok_field_helpers.h"
#include <furi_hal.h>

#define NUM_PASS_LEVELS 12
#undef TAG
#define TAG "SaflokScenePassLevels"

static void level_change_callback(OptionEntry* entry) {
    SaflokApp* app = option_entry_get_context(entry);
    if(!app) return;
    
    uint8_t level = entry->index + 1;
    
    bool enabled = (option_entry_get_current_int_value(entry) == 1);
    
    FURI_LOG_I(TAG, "Level %d changed to: %s", level, enabled ? "ON" : "OFF");
    
    int existing_index = -1;
    for(int i = 0; i < NUM_PASS_LEVELS; i++) {
        if(app->saflok_data->access_control.pass_levels[i] == level) {
            existing_index = i;
            break;
        }
        if(app->saflok_data->access_control.pass_levels[i] == 0) {
            break;
        }
    }
    
    if(enabled) {
        if(existing_index == -1) {
            int insert_pos = 0;
            while(insert_pos < NUM_PASS_LEVELS && 
                  app->saflok_data->access_control.pass_levels[insert_pos] != 0 &&
                  app->saflok_data->access_control.pass_levels[insert_pos] < level) {
                insert_pos++;
            }
            
            if(insert_pos < NUM_PASS_LEVELS) {
                if(app->saflok_data->access_control.pass_levels[insert_pos] != 0) {
                    for(int i = NUM_PASS_LEVELS - 1; i > insert_pos; i--) {
                        app->saflok_data->access_control.pass_levels[i] = 
                            app->saflok_data->access_control.pass_levels[i-1];
                    }
                }
                app->saflok_data->access_control.pass_levels[insert_pos] = level;
            }
        }
    } else {
        if(existing_index != -1) {
            for(int i = existing_index; i < NUM_PASS_LEVELS - 1; i++) {
                app->saflok_data->access_control.pass_levels[i] = 
                    app->saflok_data->access_control.pass_levels[i+1];
            }
            app->saflok_data->access_control.pass_levels[NUM_PASS_LEVELS - 1] = 0;
        }
    }
   
    if(app->saflok_data->access_control.pass_levels != saflok_packed_pass_levels(app)) {
        app->changed->pass_levels = true;
    }
    
    FURI_LOG_D(TAG, "Updated pass_levels: ");
    for(int i = 0; i < NUM_PASS_LEVELS; i++) {
        if(app->saflok_data->access_control.pass_levels[i] != 0) {
            FURI_LOG_D(TAG, "  %d", app->saflok_data->access_control.pass_levels[i]);
        }
    }
}

void saflok_scene_pass_levels_on_enter(void* context) {
    furi_assert(context);
    SaflokApp* app = context;
    
    OptionList* list = app->pass_levels_list;
    option_list_reset(list);
    
    option_list_set_header(list, "Select Enabled Levels");
    
    for(uint8_t i = 0; i < NUM_PASS_LEVELS; i++) {
        char label[16];
        snprintf(label, sizeof(label), "Level %d", i + 1);
        
        OptionEntry* entry = option_entry_alloc(
            label,
            i,
            level_change_callback,
            app);
        
        option_entry_add_int_value(entry, "OFF", 0, true);
        option_entry_add_int_value(entry, "ON", 1, true);
        
        bool level_enabled = false;
        for(uint8_t j = 0; j < NUM_PASS_LEVELS; j++) {
            if(app->saflok_data->access_control.pass_levels[j] == (i + 1)) {
                level_enabled = true;
                break;
            }
            
            if(app->saflok_data->access_control.pass_levels[j] == 0) {
                break;
            }
        }
        
        option_entry_set_current_index(entry, level_enabled ? 1 : 0);
        
        option_list_add_entry(list, entry);
    }
    
    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewPassLevelsList);
}

bool saflok_scene_pass_levels_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
    bool consumed = false;
    
    if(event.type == SceneManagerEventTypeBack) {
        consumed = scene_manager_previous_scene(app->scene_manager);
    }
    
    return consumed;
}

void saflok_scene_pass_levels_on_exit(void* context) {
    FURI_LOG_I(TAG, "Exiting pass levels scene");
    SaflokApp* app = context;
    if(!app) return;
    
    FURI_LOG_D("MemoryDebug", "Pass levels exit - before cleanup. Free heap: %u bytes", memmgr_get_free_heap());
    
    uint8_t enabled_levels[NUM_PASS_LEVELS] = {0};
    uint8_t count = 0;
    
    for(uint8_t i = 0; i < NUM_PASS_LEVELS; i++) {
        OptionEntry* entry = option_list_get_entry_by_index(app->pass_levels_list, i);
        if(entry && option_entry_get_current_int_value(entry) == 1) {
            uint8_t level = i + 1;
            enabled_levels[count++] = level;
            if(count >= NUM_PASS_LEVELS) break;
        }
    }
    
    memset(app->saflok_data->access_control.pass_levels, 0, sizeof(app->saflok_data->access_control.pass_levels));
    for(uint8_t i = 0; i < count; i++) {
        app->saflok_data->access_control.pass_levels[i] = enabled_levels[i];
    }
    
    if(app->changed->pass_levels) {
        saflok_pack_current_pass_levels(app);
    }
    
    
    FURI_LOG_D("MemoryDebug", "Pass levels exit - after cleanup. Free heap: %u bytes", memmgr_get_free_heap());
}
