#include "../subghz_i.h"
#include <loader/loader.h>
#include <storage/storage.h>

void subghz_blank_transition_draw_cb(Canvas* canvas, void* ctx);

#define SUBGHZ_HITAG2_HELL_FAP_PATH EXT_PATH("apps/Sub-GHz/fox_hitag2_hell.fap")

enum SubmenuIndex {
    SubmenuIndexHitag2Hell,
    SubmenuIndexRecover,
    SubmenuIndexRecoverBt,
    SubmenuIndexManual,
    SubmenuIndexSendRaw,
};

static void subghz_scene_fiat_v1_key_method_submenu_callback(void* context, uint32_t index) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, index);
}

void subghz_scene_fiat_v1_key_method_on_enter(void* context) {
    SubGhz* subghz = context;

    submenu_add_item(
        subghz->submenu,
        "Send Raw (Replay)",
        SubmenuIndexSendRaw,
        subghz_scene_fiat_v1_key_method_submenu_callback,
        subghz);

    {
        Storage* storage = furi_record_open(RECORD_STORAGE);
        bool has_hitag2_hell = storage_file_exists(storage, SUBGHZ_HITAG2_HELL_FAP_PATH);
        furi_record_close(RECORD_STORAGE);
        if(has_hitag2_hell) {
            submenu_add_item(
                subghz->submenu,
                "Recover Key (Hitag2Hell)",
                SubmenuIndexHitag2Hell,
                subghz_scene_fiat_v1_key_method_submenu_callback,
                subghz);
        }
    }
    submenu_add_item(
        subghz->submenu,
        "Recover Key (Auto)",
        SubmenuIndexRecover,
        subghz_scene_fiat_v1_key_method_submenu_callback,
        subghz);
    submenu_add_item(
        subghz->submenu,
        "ESP32 Recover Key",
        SubmenuIndexRecoverBt,
        subghz_scene_fiat_v1_key_method_submenu_callback,
        subghz);
    submenu_add_item(
        subghz->submenu,
        "Enter Key Manually",
        SubmenuIndexManual,
        subghz_scene_fiat_v1_key_method_submenu_callback,
        subghz);

    if(scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneFiatV1KeyMethod) ==
       SUBGHZ_FIAT_V1_KEYMETHOD_RETURN_SELECT) {
        submenu_set_selected_item(subghz->submenu, SubmenuIndexHitag2Hell);
        scene_manager_set_scene_state(subghz->scene_manager, SubGhzSceneFiatV1KeyMethod, 0);
    }

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdMenu);
}

bool subghz_scene_fiat_v1_key_method_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubmenuIndexRecover) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneFiatV1KeyMethod, SubmenuIndexRecover);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneFiatV1Recover);
            return true;
        } else if(event.event == SubmenuIndexRecoverBt) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneFiatV1KeyMethod, SubmenuIndexRecoverBt);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneFiatV1RecoverBt);
            return true;
        } else if(event.event == SubmenuIndexManual) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneFiatV1KeyMethod, SubmenuIndexManual);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneFiatV1Key);
            return true;
        } else if(event.event == SubmenuIndexSendRaw) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneFiatV1KeyMethod, SubmenuIndexSendRaw);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneFiatV1RawReplay);
            return true;
        } else if(event.event == SubmenuIndexHitag2Hell) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneFiatV1KeyMethod, SubmenuIndexHitag2Hell);

            if(!subghz->blank_transition_viewport) {
                subghz->blank_transition_viewport = view_port_alloc();
                view_port_draw_callback_set(
                    subghz->blank_transition_viewport, subghz_blank_transition_draw_cb, NULL);
                gui_add_view_port(
                    subghz->gui, subghz->blank_transition_viewport, GuiLayerFullscreen);
                view_port_update(subghz->blank_transition_viewport);
            }

            Loader* loader = furi_record_open(RECORD_LOADER);
            loader_enqueue_launch(
                loader,
                SUBGHZ_HITAG2_HELL_FAP_PATH,
                furi_string_get_cstr(subghz->file_path),
                LoaderDeferredLaunchFlagNone);
            furi_record_close(RECORD_LOADER);

            scene_manager_stop(subghz->scene_manager);
            view_dispatcher_stop(subghz->view_dispatcher);
            return true;
        }
    }

    return false;
}

void subghz_scene_fiat_v1_key_method_on_exit(void* context) {
    SubGhz* subghz = context;
    submenu_reset(subghz->submenu);
}
