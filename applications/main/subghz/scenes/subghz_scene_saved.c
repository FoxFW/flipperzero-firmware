#include "../subghz_i.h"
#include <flipper_format/flipper_format.h>
#include <storage/storage.h>
#include <stdio.h>

#define TAG "SubGhzSceneSaved"

void subghz_scene_saved_on_enter(void* context) {
    SubGhz* subghz = context;

    DialogsFileBrowserOptions browser_options;
    dialog_file_browser_set_basic_options(
        &browser_options, SUBGHZ_APP_FILENAME_EXTENSION, &I_sub1_10px);
    browser_options.base_path = SUBGHZ_APP_FOLDER;

    bool picked = dialog_file_browser_show(
        subghz->dialogs, subghz->file_path, subghz->file_path, &browser_options);

    bool loaded = false;

    if(picked) {
        loaded = subghz_key_load(subghz, furi_string_get_cstr(subghz->file_path), true);
    }

    if(loaded) {
        if(subghz_get_load_type_file(subghz) == SubGhzLoadTypeFileRaw) {
            subghz_rx_key_state_set(subghz, SubGhzRxKeyStateRAWLoad);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReadRAW);
        } else {
            subghz_rx_key_state_set(subghz, SubGhzRxKeyStateRAWLoad);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneSavedMenu);
        }
    } else {
        scene_manager_search_and_switch_to_previous_scene(subghz->scene_manager, SubGhzSceneStart);
    }
}

bool subghz_scene_saved_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void subghz_scene_saved_on_exit(void* context) {
    SubGhz* subghz = context;
    scene_manager_set_scene_state(subghz->scene_manager, SubGhzSceneSavedMenu, 0);
    UNUSED(context);
}
