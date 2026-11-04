#include "../subghz_i.h"
#include "../views/subghz_view_start_grid.h"
#include <gui/modules/fox_theme.h>
#include "subghz_scene_start.h"
#include <loader/loader.h>
#include <storage/storage.h>
#include <string.h>

#include <lib/subghz/protocols/raw.h>

#define SUBGHZ_MOD_ANALYZER_FAP_PATH  EXT_PATH("apps/Sub-GHz/subghz_modulation_analyzer.fap")
#define SUBGHZ_FREQ_ANALYZER_FAP_PATH EXT_PATH("apps/Sub-GHz/subghz_frequency_analyzer.fap")
#define SUBGHZ_RF_JAMMER_FAP_PATH     EXT_PATH("apps/Fox/fox_rf_jammer.fap")
#define SUBGHZ_TPMS_FAP_PATH          EXT_PATH("apps/Fox/fox_tpms.fap")
#define SUBGHZ_RANDOMATTACK_FAP_PATH  EXT_PATH("apps/Sub-GHz/subghz_randomattack.fap")

void subghz_blank_transition_draw_cb(Canvas* canvas, void* ctx) {
    UNUSED(ctx);
    canvas_clear(canvas);
}

void subghz_scene_start_launch_and_exit(
    SubGhz* subghz, const char* fap_path, const char* args, bool show_blank) {
    /* Pre-write the grid-focus marker so the return lands on the right button
     * even for stock apps (e.g. subghz_randomattack) that don't write it
     * themselves. Strip a leading "core:" to match subghz_app's exact checks. */
    if(args && args[0]) {
        const char* focus = args;
        if(strncmp(focus, "core:", 5) == 0) focus += 5;
        Storage* storage = furi_record_open(RECORD_STORAGE);
        File* f = storage_file_alloc(storage);
        if(storage_file_open(f, "/ext/subghz/.focus_menu", FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            storage_file_write(f, focus, strlen(focus));
        }
        storage_file_close(f);
        storage_file_free(f);
        furi_record_close(RECORD_STORAGE);
    }

    Loader* loader = furi_record_open(RECORD_LOADER);
    loader_enqueue_launch(loader, fap_path, args, LoaderDeferredLaunchFlagNone);
    furi_record_close(RECORD_LOADER);

    if(show_blank) {
        /* Hide the frozen grid during the handoff for apps with NO loader wheel.
         * Apps that DO show the wheel skip this (the wheel covers the load; the
         * extra blank otherwise flashes). */
        subghz->blank_transition_viewport = view_port_alloc();
        view_port_draw_callback_set(
            subghz->blank_transition_viewport, subghz_blank_transition_draw_cb, NULL);
        gui_add_view_port(subghz->gui, subghz->blank_transition_viewport, GuiLayerFullscreen);
        view_port_update(subghz->blank_transition_viewport);
    }

    scene_manager_stop(subghz->scene_manager);
    view_dispatcher_stop(subghz->view_dispatcher);
}

void subghz_scene_start_submenu_callback(void* context, uint32_t index) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, index);
}

void subghz_scene_start_on_enter(void* context) {
    SubGhz* subghz = context;

    if(subghz->startup_holder) {
        view_holder_set_view(subghz->startup_holder, NULL);
        view_holder_free(subghz->startup_holder);
        subghz->startup_holder = NULL;
    }
    if(subghz->startup_loading) {
        loading_free(subghz->startup_loading);
        subghz->startup_loading = NULL;
    }
    if(subghz->state_notifications == SubGhzNotificationStateStarting) {
        subghz->state_notifications = SubGhzNotificationStateIDLE;
    }
    subghz_txrx_receiver_release(subghz->txrx);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool has_freq_analyzer = storage_file_exists(storage, SUBGHZ_FREQ_ANALYZER_FAP_PATH);
    bool has_mod_analyzer  = storage_file_exists(storage, SUBGHZ_MOD_ANALYZER_FAP_PATH);
    bool has_jammer        = storage_file_exists(storage, SUBGHZ_RF_JAMMER_FAP_PATH);
    bool has_tpms          = storage_file_exists(storage, SUBGHZ_TPMS_FAP_PATH);
    bool has_subratt       = storage_file_exists(storage, SUBGHZ_RANDOMATTACK_FAP_PATH);
    bool has_keeloq        = storage_file_exists(storage, SUBGHZ_KEELOQ_TOOL_FAP_PATH);
    furi_record_close(RECORD_STORAGE);

    subghz_start_grid_set_visible(subghz->start_grid, SGRID_IDX_FREQANA, has_freq_analyzer);
    subghz_start_grid_set_visible(subghz->start_grid, SGRID_IDX_MODANA,  has_mod_analyzer);

    subghz_start_grid_set_visible(subghz->start_grid, SGRID_IDX_RADIOSETTINGS, true);
    subghz_start_grid_set_visible(subghz->start_grid, SGRID_IDX_JAMMER,  has_jammer);
    subghz_start_grid_set_visible(subghz->start_grid, SGRID_IDX_SUBRATT, has_subratt);
    subghz_start_grid_set_visible(subghz->start_grid, SGRID_IDX_TPMS,    has_tpms);
    subghz_start_grid_set_visible(subghz->start_grid, SGRID_IDX_KEELOQ,   has_keeloq);
    subghz_start_grid_set_visible(subghz->start_grid, SGRID_IDX_KEELOQBF, has_keeloq);

    subghz_start_grid_set_callback(
        subghz->start_grid,
        subghz_scene_start_submenu_callback,
        subghz);

    {
        uint32_t focus =
            scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneStart);
        uint8_t grid_btn = 0;
        if     (focus == SubmenuIndexSaved)                grid_btn = SGRID_IDX_SAVED;
        else if(focus == SubmenuIndexReadRAW)              grid_btn = SGRID_IDX_READRAW;
        else if(focus == SubmenuIndexAddManuallyAdvanced)  grid_btn = SGRID_IDX_ADDMAN;
        else if(focus == SubmenuIndexFrequencyAnalyzer)    grid_btn = SGRID_IDX_FREQANA;
        else if(focus == SubmenuIndexModulationAnalyzer)   grid_btn = SGRID_IDX_MODANA;
        else if(focus == SubmenuIndexProtocolList)         grid_btn = SGRID_IDX_PROTOCOLS;
        else if(focus == SubmenuIndexModulationList)       grid_btn = SGRID_IDX_MODLIST;
        else if(focus == SubmenuIndexExtSettings)          grid_btn = SGRID_IDX_RADIOSETTINGS;
        else if(focus == SubmenuIndexRFJammer)             grid_btn = SGRID_IDX_JAMMER;
        else if(focus == SubmenuIndexSubRATT)              grid_btn = SGRID_IDX_SUBRATT;
        else if(focus == SubmenuIndexTpmsReader)           grid_btn = SGRID_IDX_TPMS;
        else if(focus == SubmenuIndexKeeloqKeys)           grid_btn = SGRID_IDX_KEELOQ;
        else if(focus == SubmenuIndexKeeloqBf2)            grid_btn = SGRID_IDX_KEELOQBF;
        else if(focus == SubmenuIndexReadDual)             grid_btn = SGRID_IDX_READDUAL;
        subghz_start_grid_set_selected(subghz->start_grid, grid_btn);
    }

    if(fox_theme_is_active()) {

        view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdStartGrid);
    } else {

        uint32_t focus =
            scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneStart);
        submenu_reset(subghz->submenu);
        submenu_add_item(subghz->submenu, "Read", SubmenuIndexRead,
            subghz_scene_start_submenu_callback, subghz);
        submenu_add_item(subghz->submenu, "Read Int/Ext", SubmenuIndexReadDual,
            subghz_scene_start_submenu_callback, subghz);
        submenu_add_item(subghz->submenu, "Read RAW", SubmenuIndexReadRAW,
            subghz_scene_start_submenu_callback, subghz);
        submenu_add_item(subghz->submenu, "Saved", SubmenuIndexSaved,
            subghz_scene_start_submenu_callback, subghz);
        submenu_add_item(subghz->submenu, "Add Manually", SubmenuIndexAddManuallyAdvanced,
            subghz_scene_start_submenu_callback, subghz);
        if(has_freq_analyzer)
            submenu_add_item(subghz->submenu, "Freq. Analyzer", SubmenuIndexFrequencyAnalyzer,
                subghz_scene_start_submenu_callback, subghz);
        if(has_mod_analyzer)
            submenu_add_item(subghz->submenu, "Mod. Analyzer", SubmenuIndexModulationAnalyzer,
                subghz_scene_start_submenu_callback, subghz);
        submenu_add_item(subghz->submenu, "Protocols", SubmenuIndexProtocolList,
            subghz_scene_start_submenu_callback, subghz);
        submenu_add_item(subghz->submenu, "Modulations", SubmenuIndexModulationList,
            subghz_scene_start_submenu_callback, subghz);
        submenu_add_item(subghz->submenu, "Radio Settings", SubmenuIndexExtSettings,
            subghz_scene_start_submenu_callback, subghz);
        if(has_jammer)
            submenu_add_item(subghz->submenu, "RF Jammer", SubmenuIndexRFJammer,
                subghz_scene_start_submenu_callback, subghz);
        if(has_subratt)
            submenu_add_item(subghz->submenu, "SubRATT", SubmenuIndexSubRATT,
                subghz_scene_start_submenu_callback, subghz);
        if(has_tpms)
            submenu_add_item(subghz->submenu, "TPMS Reader", SubmenuIndexTpmsReader,
                subghz_scene_start_submenu_callback, subghz);
        submenu_set_selected_item(subghz->submenu, focus);
        view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdMenu);
    }
}

bool subghz_scene_start_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    if(event.type == SceneManagerEventTypeBack) {
        if(!scene_manager_previous_scene(subghz->scene_manager)) {

            scene_manager_stop(subghz->scene_manager);
            view_dispatcher_stop(subghz->view_dispatcher);
        }
        return true;
    } else if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubmenuIndexReadRAW) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexReadRAW);
            subghz_rx_key_state_set(subghz, SubGhzRxKeyStateIDLE);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReadRAW);
            return true;
        } else if(event.event == SubmenuIndexRead) {
            subghz_rx_key_state_set(subghz, SubGhzRxKeyStateIDLE);
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexRead);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReceiver);
            return true;
        } else if(event.event == SubmenuIndexReadDual) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexReadDual);
            if(!subghz_txrx_dual_is_ext_connected(subghz->txrx)) {
                subghz_dialog_message_no_ext_module(subghz);
                return true;
            }
            subghz_rx_key_state_set(subghz, SubGhzRxKeyStateIDLE);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReceiverDual);
            return true;
        } else if(event.event == SubmenuIndexSaved) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexSaved);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneSaved);
            return true;
        } else if(event.event == SubmenuIndexAddManuallyAdvanced) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexAddManuallyAdvanced);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneSetType);
            return true;
        } else if(event.event == SubmenuIndexFrequencyAnalyzer) {

            subghz_scene_start_launch_and_exit(
                subghz, SUBGHZ_FREQ_ANALYZER_FAP_PATH, "core:menu:freq", true);
            return true;
        } else if(event.event == SubmenuIndexModulationAnalyzer) {

            subghz_scene_start_launch_and_exit(
                subghz, SUBGHZ_MOD_ANALYZER_FAP_PATH, "core:menu:mod", true);
            return true;
        } else if(event.event == SubmenuIndexProtocolList) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexProtocolList);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneProtocolList);
        } else if(event.event == SubmenuIndexModulationList) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexModulationList);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneModulationList);
            return true;
        } else if(event.event == SubmenuIndexExtSettings) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexExtSettings);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneExtModuleSettings);
            return true;
        } else if(event.event == SubmenuIndexRFJammer) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexRFJammer);
            subghz_scene_start_launch_and_exit(subghz, SUBGHZ_RF_JAMMER_FAP_PATH, "menu:jammer", true);
            return true;
        } else if(event.event == SubmenuIndexSubRATT) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexSubRATT);
            subghz_scene_start_launch_and_exit(
                subghz, SUBGHZ_RANDOMATTACK_FAP_PATH, "core:menu:bruteforcer", true);
            return true;
        } else if(event.event == SubmenuIndexTpmsReader) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexTpmsReader);
            subghz_scene_start_launch_and_exit(subghz, SUBGHZ_TPMS_FAP_PATH, "core:menu:tpms", true);
            return true;
        } else if(event.event == SubmenuIndexKeeloqKeys) {
            subghz_scene_start_launch_and_exit(
                subghz, SUBGHZ_KEELOQ_TOOL_FAP_PATH, "core:menu:keeloq_keys", true);
            return true;
        } else if(event.event == SubmenuIndexKeeloqBf2) {
            subghz_scene_start_launch_and_exit(
                subghz, SUBGHZ_KEELOQ_TOOL_FAP_PATH, "core:menu:keeloq_bf", true);
            return true;
        }
    }
    return false;
}

void subghz_scene_start_on_exit(void* context) {
    SubGhz* subghz = context;

    submenu_reset(subghz->submenu);
}
