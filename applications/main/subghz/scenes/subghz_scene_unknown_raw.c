#include "../subghz_i.h"

/* Per-item menu for an "Unknown RAW" history entry (an RF burst captured during
 * live Read that none of the registered decoders recognised). Reached from the
 * receiver list when the selected item's label is "Unknown RAW". */

#define TAG "SubGhzSceneUnknownRaw"

#define SUBGHZ_UNKNOWN_RAW_TMP_PATH \
    SUBGHZ_APP_FOLDER "/.tmp_unknown_raw" SUBGHZ_APP_FILENAME_EXTENSION

typedef enum {
    SubGhzUnknownRawEmulate,
    SubGhzUnknownRawSave,
    SubGhzUnknownRawMatch,
    SubGhzUnknownRawDiscard,
} SubGhzUnknownRawIndex;

static void subghz_scene_unknown_raw_submenu_callback(void* context, uint32_t index) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, index);
}

static void subghz_scene_unknown_raw_emulate(SubGhz* subghz, uint16_t idx) {
    if(!subghz_history_copy_item_to_file(subghz->history, idx, SUBGHZ_UNKNOWN_RAW_TMP_PATH)) return;

    subghz->state_notifications = SubGhzNotificationStateIDLE;
    subghz_txrx_hopper_set_state(subghz->txrx, SubGhzHopperStateOFF);
    subghz_txrx_stop(subghz->txrx);
    furi_string_set(subghz->file_path, SUBGHZ_UNKNOWN_RAW_TMP_PATH);
    if(subghz_key_load(subghz, SUBGHZ_UNKNOWN_RAW_TMP_PATH, false)) {
        if(subghz_get_load_type_file(subghz) == SubGhzLoadTypeFileRaw) {
            scene_manager_set_scene_state(subghz->scene_manager, SubGhzSceneTransmitter, 1);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneTransmitter);
        }
    }
}

void subghz_scene_unknown_raw_on_enter(void* context) {
    SubGhz* subghz = context;
    Submenu* submenu = subghz->submenu;

    if(subghz->raw_match) {
        subghz_raw_match_free(subghz->raw_match);
        subghz->raw_match = NULL;
    }
    scene_manager_set_scene_state(subghz->scene_manager, SubGhzSceneUnknownRawMatch, 0);

    submenu_reset(submenu);
    submenu_add_item(
        submenu, "Emulate RAW", SubGhzUnknownRawEmulate,
        subghz_scene_unknown_raw_submenu_callback, subghz);
    submenu_add_item(
        submenu, "Save RAW", SubGhzUnknownRawSave,
        subghz_scene_unknown_raw_submenu_callback, subghz);
    submenu_add_item(
        submenu, "Closest Protocol", SubGhzUnknownRawMatch,
        subghz_scene_unknown_raw_submenu_callback, subghz);
    submenu_add_item(
        submenu, "Discard", SubGhzUnknownRawDiscard,
        subghz_scene_unknown_raw_submenu_callback, subghz);
    submenu_set_selected_item(submenu, SubGhzUnknownRawEmulate);

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdMenu);
}

bool subghz_scene_unknown_raw_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    if(event.type == SceneManagerEventTypeCustom) {
        uint16_t idx = subghz->idx_menu_chosen;
        switch(event.event) {
        case SubGhzUnknownRawEmulate:
            subghz_scene_unknown_raw_emulate(subghz, idx);
            return true;
        case SubGhzUnknownRawSave:
            /* SaveName's default branch writes subghz_history_get_raw_data(idx_menu_chosen)
             * straight to the chosen file -- no decoder needed for a RAW item. */
            subghz->state_notifications = SubGhzNotificationStateIDLE;
            subghz_txrx_hopper_set_state(subghz->txrx, SubGhzHopperStateOFF);
            subghz_txrx_stop(subghz->txrx);
            subghz_file_name_clear(subghz);
            subghz->save_datetime = subghz_history_get_datetime(subghz->history, idx);
            subghz->save_datetime_set = true;
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneSaveName);
            return true;
        case SubGhzUnknownRawMatch:
            subghz->state_notifications = SubGhzNotificationStateIDLE;
            subghz_txrx_hopper_set_state(subghz->txrx, SubGhzHopperStateOFF);
            subghz_txrx_stop(subghz->txrx);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneUnknownRawMatch);
            return true;
        case SubGhzUnknownRawDiscard:
            subghz_history_delete_item(subghz->history, idx);
            /* Receiver on_enter rebuilds its list from history, so the item is gone. */
            scene_manager_previous_scene(subghz->scene_manager);
            return true;
        default:
            break;
        }
    }
    return false;
}

void subghz_scene_unknown_raw_on_exit(void* context) {
    SubGhz* subghz = context;
    submenu_reset(subghz->submenu);
}
