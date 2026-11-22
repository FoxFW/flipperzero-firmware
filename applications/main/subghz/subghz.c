#include <furi/core/log.h>
#include <subghz/types.h>
#include <lib/toolbox/path.h>
#include <float_tools.h>
#include "subghz_i.h"
#include "helpers/subghz_decode_plugin_loader.h"
#include "helpers/subghz_config_scene_loader.h"
#include "scenes/subghz_scene_start.h"
#include <storage/storage.h>
#include <loader/loader.h>
#include <furi/core/memmgr.h>
#include "helpers/subghz_lowram_guard.h"

#define TAG "SubGhzApp"

bool subghz_scene_set_type_generate_protocol_from_infos(SubGhz* subghz);

#define SUBGHZ_ADD_MANUAL_HANDOFF_PATH EXT_PATH("subghz/assets/.add_manual")

typedef struct {
    uint32_t set_type;
    uint32_t serial;
    uint32_t btn;
    uint32_t cnt;
    uint32_t seed;
    uint64_t key;
    uint32_t bits;
    uint32_t te;
    uint32_t frequency;
} SubGhzAddManualHandoff;

static bool subghz_add_manual_read_handoff(Storage* storage, SubGhzAddManualHandoff* out) {

    if(!storage_file_exists(storage, SUBGHZ_ADD_MANUAL_HANDOFF_PATH)) return false;
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool ok = false;
    do {
        if(!flipper_format_file_open_existing(ff, SUBGHZ_ADD_MANUAL_HANDOFF_PATH)) break;
        if(!flipper_format_read_uint32(ff, "SetType", &out->set_type, 1)) break;
        uint32_t tmp;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Serial", &tmp, 1)) out->serial = tmp;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Btn", &tmp, 1)) out->btn = tmp;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Cnt", &tmp, 1)) out->cnt = tmp;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Seed", &tmp, 1)) out->seed = tmp;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Bits", &tmp, 1)) out->bits = tmp;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "TE", &tmp, 1)) out->te = tmp;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Frequency", &tmp, 1)) out->frequency = tmp;
        flipper_format_rewind(ff);
        uint8_t key_bytes[8] = {0};
        if(flipper_format_read_hex(ff, "Key", key_bytes, 8)) {
            out->key = 0;
            for(size_t i = 0; i < 8; i++) {
                out->key = (out->key << 8) | key_bytes[i];
            }
        }
        ok = true;
    } while(false);
    flipper_format_free(ff);
    return ok;
}

static void subghz_add_manual_apply(SubGhz* subghz, const SubGhzAddManualHandoff* h) {
    subghz_gen_info_reset(subghz->gen_info);
    subghz_scene_set_type_fill_generation_infos(subghz->gen_info, (SetType)h->set_type);
    GenInfo* g = subghz->gen_info;
    if(h->frequency) g->freq = h->frequency;
    switch(g->type) {
    case GenData:
        g->data.key = h->key;
        if(h->bits) g->data.bits = (uint8_t)h->bits;
        if(h->te) g->data.te = h->te;
        break;
    case GenFaacSLH:
        g->faac_slh.serial = h->serial;
        g->faac_slh.btn = (uint8_t)h->btn;
        g->faac_slh.cnt = h->cnt;
        g->faac_slh.seed = h->seed;
        break;
    case GenKeeloq:
        g->keeloq.serial = h->serial;
        g->keeloq.btn = (uint8_t)h->btn;
        g->keeloq.cnt = (uint16_t)h->cnt;
        break;
    case GenCameAtomo:
        g->came_atomo.serial = h->serial;
        g->came_atomo.cnt = (uint16_t)h->cnt;
        break;
    case GenKeeloqBFT:
        g->keeloq_bft.serial = h->serial;
        g->keeloq_bft.btn = (uint8_t)h->btn;
        g->keeloq_bft.cnt = (uint16_t)h->cnt;
        g->keeloq_bft.seed = h->seed;
        break;
    case GenAlutechAt4n:
        g->alutech_at_4n.serial = h->serial;
        g->alutech_at_4n.btn = (uint8_t)h->btn;
        g->alutech_at_4n.cnt = (uint16_t)h->cnt;
        break;
    case GenSomfyTelis:
        g->somfy_telis.serial = h->serial;
        g->somfy_telis.btn = (uint8_t)h->btn;
        g->somfy_telis.cnt = (uint16_t)h->cnt;
        break;
    case GenKingGatesStylo4k:
        g->kinggates_stylo_4k.serial = h->serial;
        g->kinggates_stylo_4k.btn = (uint8_t)h->btn;
        g->kinggates_stylo_4k.cnt = (uint16_t)h->cnt;
        break;
    case GenBenincaARC:
        g->beninca_arc.serial = h->serial;
        g->beninca_arc.btn = (uint8_t)h->btn;
        g->beninca_arc.cnt = h->cnt;
        break;
    case GenJarolift:
        g->jarolift.serial = h->serial;
        g->jarolift.btn = (uint8_t)h->btn;
        g->jarolift.cnt = (uint16_t)h->cnt;
        break;
    case GenNiceFlorS:
        g->nice_flor_s.serial = h->serial;
        g->nice_flor_s.btn = (uint8_t)h->btn;
        g->nice_flor_s.cnt = (uint16_t)h->cnt;
        break;
    case GenSecPlus2:
        g->sec_plus_2.serial = h->serial;
        g->sec_plus_2.btn = (uint8_t)h->btn;
        g->sec_plus_2.cnt = h->cnt;
        break;
    case GenPhoenixV2:
        g->phoenix_v2.serial = h->serial;
        g->phoenix_v2.cnt = (uint16_t)h->cnt;
        break;
    case GenSecPlus1:
    default:
        break;
    }
}

static bool subghz_protocol_enabled_callback(void* context, size_t registry_index, const char* protocol_name) {
    UNUSED(protocol_name);
    SubGhz* subghz = context;
    return subghz_protocol_filter_is_enabled(subghz->protocol_filter, registry_index);
}

bool subghz_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    SubGhz* subghz = context;
    return scene_manager_handle_custom_event(subghz->scene_manager, event);
}

bool subghz_back_event_callback(void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    return scene_manager_handle_back_event(subghz->scene_manager);
}

void subghz_tick_event_callback(void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    scene_manager_handle_tick_event(subghz->scene_manager);
}

static void subghz_rpc_command_callback(const RpcAppSystemEvent* event, void* context) {
    furi_assert(context);
    SubGhz* subghz = context;

    furi_assert(subghz->rpc_ctx);

    if(event->type == RpcAppEventTypeSessionClose) {
        view_dispatcher_send_custom_event(
            subghz->view_dispatcher, SubGhzCustomEventSceneRpcSessionClose);
        rpc_system_app_set_callback(subghz->rpc_ctx, NULL, NULL);
        subghz->rpc_ctx = NULL;
    } else if(event->type == RpcAppEventTypeAppExit) {
        view_dispatcher_send_custom_event(subghz->view_dispatcher, SubGhzCustomEventSceneExit);
    } else if(event->type == RpcAppEventTypeLoadFile) {
        furi_assert(event->data.type == RpcAppSystemEventDataTypeString);
        furi_string_set(subghz->file_path, event->data.string);
        view_dispatcher_send_custom_event(subghz->view_dispatcher, SubGhzCustomEventSceneRpcLoad);
    } else if(event->type == RpcAppEventTypeButtonPress) {
        view_dispatcher_send_custom_event(
            subghz->view_dispatcher, SubGhzCustomEventSceneRpcButtonPress);
    } else if(event->type == RpcAppEventTypeButtonRelease) {
        view_dispatcher_send_custom_event(
            subghz->view_dispatcher, SubGhzCustomEventSceneRpcButtonRelease);
    } else if(event->type == RpcAppEventTypeButtonPressRelease) {
        view_dispatcher_send_custom_event(
            subghz->view_dispatcher, SubGhzCustomEventSceneRpcButtonPressRelease);
    } else {
        rpc_system_app_confirm(subghz->rpc_ctx, false);
    }
}

void subghz_save_all(SubGhz* subghz) {
    furi_assert(subghz);

    subghz_protocol_filter_get_raw(
        subghz->protocol_filter,
        subghz->last_settings->protocol_filter_data,
        sizeof(subghz->last_settings->protocol_filter_data));
    subghz->last_settings->protocol_filter_present = true;
    subghz_modulation_filter_get_raw(
        subghz->modulation_filter,
        subghz->last_settings->mod_filter_data,
        sizeof(subghz->last_settings->mod_filter_data));
    subghz->last_settings->mod_filter_present = true;

    subghz_last_settings_save(subghz->last_settings);
}

SubGhz* subghz_alloc(bool alloc_for_tx_only) {
    SubGhz* subghz = calloc(1, sizeof(SubGhz));

    subghz_decode_plugin_loader_install();

    subghz->blank_transition_viewport = NULL;
    subghz->loading_cover_viewport = NULL;
    subghz->loading_cover_text = NULL;
    subghz->loaded_file_has_raw = false;

    subghz->file_path = furi_string_alloc();
    subghz->saved_preselect = furi_string_alloc();
    subghz->raw_send_title = furi_string_alloc();
    subghz->decode_raw_source = furi_string_alloc();
    subghz->file_path_tmp = furi_string_alloc();
    subghz->decoded_preview_orig_path = furi_string_alloc();
    subghz->decoded_preview_active    = false;

    subghz->gui = furi_record_open(RECORD_GUI);

    if(!alloc_for_tx_only) {
        subghz->startup_loading = loading_alloc();
        subghz->startup_holder  = view_holder_alloc();
        view_holder_attach_to_gui(subghz->startup_holder, subghz->gui);
        view_holder_set_view(
            subghz->startup_holder, loading_get_view(subghz->startup_loading));
    }

    subghz->view_dispatcher = view_dispatcher_alloc();

    subghz->scene_manager = scene_manager_alloc(&subghz_scene_handlers, subghz);
    view_dispatcher_set_event_callback_context(subghz->view_dispatcher, subghz);
    view_dispatcher_set_custom_event_callback(
        subghz->view_dispatcher, subghz_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        subghz->view_dispatcher, subghz_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        subghz->view_dispatcher, subghz_tick_event_callback, 100);

    subghz->notifications = furi_record_open(RECORD_NOTIFICATION);
#if SUBGHZ_MEASURE_LOADING
    uint32_t load_ticks = furi_get_tick();
#endif
    subghz->txrx = subghz_txrx_alloc();

    if(!alloc_for_tx_only) {

        subghz->submenu = submenu_alloc();
        view_dispatcher_add_view(
            subghz->view_dispatcher, SubGhzViewIdMenu, submenu_get_view(subghz->submenu));

        subghz->subghz_receiver = subghz_view_receiver_alloc();
        view_dispatcher_add_view(
            subghz->view_dispatcher,
            SubGhzViewIdReceiver,
            subghz_view_receiver_get_view(subghz->subghz_receiver));
    }

    subghz->popup = popup_alloc();
    view_dispatcher_add_view(
        subghz->view_dispatcher, SubGhzViewIdPopup, popup_get_view(subghz->popup));
    if(!alloc_for_tx_only) {

        subghz->text_input = text_input_alloc();
        view_dispatcher_add_view(
            subghz->view_dispatcher,
            SubGhzViewIdTextInput,
            text_input_get_view(subghz->text_input));

        subghz->byte_input = byte_input_alloc();
        view_dispatcher_add_view(
            subghz->view_dispatcher,
            SubGhzViewIdByteInput,
            byte_input_get_view(subghz->byte_input));

        subghz->number_input = number_input_alloc();
        view_dispatcher_add_view(
            subghz->view_dispatcher,
            SubGhzViewIdNumberInput,
            number_input_get_view(subghz->number_input));

        subghz->widget = widget_alloc();
        view_dispatcher_add_view(
            subghz->view_dispatcher, SubGhzViewIdWidget, widget_get_view(subghz->widget));
    }

    subghz->dialogs = furi_record_open(RECORD_DIALOGS);

    subghz->subghz_transmitter = subghz_view_transmitter_alloc();
    view_dispatcher_add_view(
        subghz->view_dispatcher,
        SubGhzViewIdTransmitter,
        subghz_view_transmitter_get_view(subghz->subghz_transmitter));
    if(!alloc_for_tx_only) {

        subghz->variable_item_list = variable_item_list_alloc();
        view_dispatcher_add_view(
            subghz->view_dispatcher,
            SubGhzViewIdVariableItemList,
            variable_item_list_get_view(subghz->variable_item_list));

        subghz->subghz_signal_visualizer = subghz_signal_visualizer_alloc(subghz->txrx);
        view_dispatcher_add_view(
            subghz->view_dispatcher,
            SubGhzViewIdSignalVisualizer,
            subghz_signal_visualizer_get_view(subghz->subghz_signal_visualizer));
    }

    subghz->subghz_read_raw = subghz_read_raw_alloc(alloc_for_tx_only);
    view_dispatcher_add_view(
        subghz->view_dispatcher,
        SubGhzViewIdReadRAW,
        subghz_read_raw_get_view(subghz->subghz_read_raw));

    subghz->start_grid = subghz_start_grid_alloc();
    view_dispatcher_add_view(
        subghz->view_dispatcher,
        SubGhzViewIdStartGrid,
        subghz_start_grid_get_view(subghz->start_grid));

    subghz->receiver_info_bar = subghz_receiver_info_bar_alloc();
    view_dispatcher_add_view(
        subghz->view_dispatcher,
        SubGhzViewIdReceiverInfoBar,
        subghz_receiver_info_bar_get_view(subghz->receiver_info_bar));

    subghz->subghz_psa_decrypt = subghz_view_psa_decrypt_alloc();
    view_dispatcher_add_view(
        subghz->view_dispatcher,
        SubGhzViewIdPsaDecrypt,
        subghz_view_psa_decrypt_get_view(subghz->subghz_psa_decrypt));

    subghz->subghz_seed_bf = subghz_view_seed_bf_alloc();
    view_dispatcher_add_view(
        subghz->view_dispatcher,
        SubGhzViewIdSeedBf,
        subghz_view_seed_bf_get_view(subghz->subghz_seed_bf));

    subghz->subghz_fiat_v1_recover = subghz_view_fiat_v1_recover_alloc();
    view_dispatcher_add_view(
        subghz->view_dispatcher,
        SubGhzViewIdFiatV1Recover,
        subghz_view_fiat_v1_recover_get_view(subghz->subghz_fiat_v1_recover));

    subghz->threshold_rssi = subghz_threshold_rssi_alloc();

    subghz_unlock(subghz);

    subghz->last_settings = subghz_last_settings_alloc();
    subghz->protocol_filter = subghz_protocol_filter_alloc();
    subghz->modulation_filter = subghz_modulation_filter_alloc();

    subghz_last_settings_load(subghz->last_settings, 0);

    if(subghz->last_settings->protocol_filter_present) {
        subghz_protocol_filter_set_raw(
            subghz->protocol_filter,
            subghz->last_settings->protocol_filter_data,
            sizeof(subghz->last_settings->protocol_filter_data));
    }
    if(subghz->last_settings->mod_filter_present) {
        subghz_modulation_filter_set_raw(
            subghz->modulation_filter,
            subghz->last_settings->mod_filter_data,
            sizeof(subghz->last_settings->mod_filter_data));
    }
    subghz_txrx_set_protocol_enabled_callback(
        subghz->txrx, subghz_protocol_enabled_callback, subghz);

    furi_hal_subghz_set_ext_leds_and_amp(subghz->last_settings->leds_and_amp);

    subghz_txrx_set_frequency_offset(subghz->txrx, subghz->last_settings->frequency_offset);

    if(!alloc_for_tx_only) {
        subghz_txrx_set_preset_internal(
            subghz->txrx,
            subghz->last_settings->frequency,
            subghz->last_settings->preset_index,
            subghz->tx_power);
        subghz->history = subghz_history_alloc();
        subghz->raw_match = NULL;
        subghz_view_receiver_set_item_callback(
            subghz->subghz_receiver, subghz_history_get_menu_item, subghz->history);
    }

    subghz_rx_key_state_set(subghz, SubGhzRxKeyStateIDLE);

    subghz->gen_info = malloc(sizeof(GenInfo));

    if(!alloc_for_tx_only) {
        subghz->ignore_filter = subghz->last_settings->ignore_filter;
        subghz->filter = subghz->last_settings->filter;
        subghz->tx_power = subghz->last_settings->tx_power;
    } else {
        subghz->filter = SubGhzProtocolFlag_Decodable;
        subghz->ignore_filter = 0x0;
        subghz->tx_power = 0;
    }

    subghz_txrx_receiver_set_filter(subghz->txrx, subghz->filter);
    subghz_txrx_set_need_save_callback(subghz->txrx, subghz_save_to_file, subghz);

    if(!alloc_for_tx_only) {
        if(!float_is_equal(subghz->last_settings->rssi, 0)) {
            subghz_threshold_rssi_set(subghz->threshold_rssi, subghz->last_settings->rssi);
        } else {
            subghz->last_settings->rssi = SUBGHZ_LAST_SETTING_FREQUENCY_ANALYZER_TRIGGER;
        }
    }
#if SUBGHZ_MEASURE_LOADING
    load_ticks = furi_get_tick() - load_ticks;
    FURI_LOG_I(TAG, "Loaded: %ld ms.", load_ticks);
#endif

    subghz->error_str = furi_string_alloc();

    return subghz;
}

void subghz_free(SubGhz* subghz, bool alloc_for_tx_only) {
    furi_assert(subghz);

    if(subghz->rpc_ctx) {
        rpc_system_app_set_callback(subghz->rpc_ctx, NULL, NULL);
        rpc_system_app_send_exited(subghz->rpc_ctx);
        subghz_blink_stop(subghz);
        subghz->rpc_ctx = NULL;
    }

    subghz_txrx_speaker_off(subghz->txrx);
    subghz_txrx_stop(subghz->txrx);
    subghz_txrx_sleep(subghz->txrx);

    if(!alloc_for_tx_only) {

        view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdReceiver);
        subghz_view_receiver_free(subghz->subghz_receiver);

        view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdTextInput);
        text_input_free(subghz->text_input);

        view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdByteInput);
        byte_input_free(subghz->byte_input);

        view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdNumberInput);
        number_input_free(subghz->number_input);

        view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdWidget);
        widget_free(subghz->widget);
    }

    furi_record_close(RECORD_DIALOGS);

    view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdTransmitter);
    subghz_view_transmitter_free(subghz->subghz_transmitter);
    if(!alloc_for_tx_only) {

        view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdVariableItemList);
        variable_item_list_free(subghz->variable_item_list);

        view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdSignalVisualizer);
        subghz_signal_visualizer_free(subghz->subghz_signal_visualizer);
    }

    view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdPsaDecrypt);
    subghz_view_psa_decrypt_free(subghz->subghz_psa_decrypt);

    view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdSeedBf);
    subghz_view_seed_bf_free(subghz->subghz_seed_bf);

    view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdFiatV1Recover);
    subghz_view_fiat_v1_recover_free(subghz->subghz_fiat_v1_recover);

    view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdReadRAW);
    view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdStartGrid);
    subghz_start_grid_free(subghz->start_grid);
    view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdReceiverInfoBar);
    subghz_receiver_info_bar_free(subghz->receiver_info_bar);
    subghz_read_raw_free(subghz->subghz_read_raw);
    if(!alloc_for_tx_only) {

        view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdMenu);
        submenu_free(subghz->submenu);
    }

    view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdPopup);
    popup_free(subghz->popup);

    scene_manager_free(subghz->scene_manager);

    view_dispatcher_free(subghz->view_dispatcher);

    if(subghz->blank_transition_viewport) {
        gui_remove_view_port(subghz->gui, subghz->blank_transition_viewport);
        view_port_free(subghz->blank_transition_viewport);
        subghz->blank_transition_viewport = NULL;
    }

    if(subghz->loading_cover_viewport) {
        gui_remove_view_port(subghz->gui, subghz->loading_cover_viewport);
        view_port_free(subghz->loading_cover_viewport);
        subghz->loading_cover_viewport = NULL;
    }

    if(subghz->recheck_cover_timer) {
        furi_timer_stop(subghz->recheck_cover_timer);
        furi_timer_free(subghz->recheck_cover_timer);
        subghz->recheck_cover_timer = NULL;
    }
    if(subghz->recheck_cover_viewport) {
        gui_remove_view_port(subghz->gui, subghz->recheck_cover_viewport);
        view_port_free(subghz->recheck_cover_viewport);
        subghz->recheck_cover_viewport = NULL;
    }

    if(subghz->startup_holder) {
        view_holder_set_view(subghz->startup_holder, NULL);
        view_holder_free(subghz->startup_holder);
        subghz->startup_holder = NULL;
    }
    if(subghz->startup_loading) {
        loading_free(subghz->startup_loading);
        subghz->startup_loading = NULL;
    }
    furi_record_close(RECORD_GUI);
    subghz->gui = NULL;

    subghz_save_all(subghz);
    subghz_protocol_filter_free(subghz->protocol_filter);
    subghz_modulation_filter_free(subghz->modulation_filter);
    subghz_last_settings_free(subghz->last_settings);

    subghz_threshold_rssi_free(subghz->threshold_rssi);

    if(!alloc_for_tx_only) {
        if(subghz->raw_match) subghz_raw_match_free(subghz->raw_match);
        subghz_history_free(subghz->history);
    }

    free(subghz->gen_info);

    subghz_decode_plugin_loader_flush();
    subghz_config_scene_loader_flush();
    subghz_txrx_free(subghz->txrx);

    furi_string_free(subghz->error_str);

    furi_record_close(RECORD_NOTIFICATION);
    subghz->notifications = NULL;

    furi_string_free(subghz->file_path);
    furi_string_free(subghz->saved_preselect);
    furi_string_free(subghz->raw_send_title);
    furi_string_free(subghz->decode_raw_source);
    furi_string_free(subghz->file_path_tmp);
    furi_string_free(subghz->decoded_preview_orig_path);

    free(subghz);
}

static void subghz_ram_recovery_restart(void) {
    memmgr_defrag_suppress_once();
    subghz_lowram_softlock_engage();
    Loader* loader = furi_record_open(RECORD_LOADER);
    loader_enqueue_launch(loader, "subghz", "ramrecover", LoaderDeferredLaunchFlagNone);
    furi_record_close(RECORD_LOADER);
}

int32_t subghz_app(void* p) {

    static char focus_file_buf[256];
    uint32_t menu_focus_index = 0;
    bool focus_file_existed = false;
    char focus_menu_content[32] = {0};
    bool is_keymethod_return = false;
    bool is_inforeturn = false;
    SubGhzAddManualHandoff add_manual_handoff = {0};
    bool is_add_manual = false;

    bool is_ram_recover_pass = (p && strcmp((const char*)p, "ramrecover") == 0);
    if(is_ram_recover_pass) {
        subghz_lowram_softlock_release();
        p = NULL;
    }

    if(p && strlen((const char*)p)) {
        const char* fp = (const char*)p;
        if(strncmp(fp, "core:", 5) == 0) fp += 5;
        uint32_t fi = 0;
        if(strcmp(fp, "menu:freq") == 0) fi = SubmenuIndexFrequencyAnalyzer;
        else if(strcmp(fp, "menu:mod") == 0) fi = SubmenuIndexModulationAnalyzer;
        else if(strcmp(fp, "menu:jammer") == 0) fi = SubmenuIndexRFJammer;
        else if(strcmp(fp, "menu:tpms") == 0) fi = SubmenuIndexTpmsReader;
        else if(strcmp(fp, "menu:bruteforcer") == 0) fi = SubmenuIndexSubRATT;
        if(fi != 0) {
            menu_focus_index = fi;
            p = NULL;
        }
    }

    if(!p || strlen((const char*)p) == 0) {
        Storage* storage = furi_record_open(RECORD_STORAGE);

        if(storage_file_exists(storage, "/ext/subghz/.focus_menu")) {
            File* f = storage_file_alloc(storage);
            if(storage_file_open(f, "/ext/subghz/.focus_menu", FSAM_READ, FSOM_OPEN_EXISTING)) {
                char buf[32] = {0};
                uint16_t read = storage_file_read(f, buf, sizeof(buf) - 1);
                buf[read] = '\0';
                strncpy(focus_menu_content, buf, sizeof(focus_menu_content) - 1);
                                if(strcmp(buf, "menu:freq") == 0) {
                    menu_focus_index = SubmenuIndexFrequencyAnalyzer;
                } else if(strcmp(buf, "menu:mod") == 0) {
                    menu_focus_index = SubmenuIndexModulationAnalyzer;
                } else if(strcmp(buf, "menu:jammer") == 0) {
                    menu_focus_index = SubmenuIndexRFJammer;
                } else if(strcmp(buf, "menu:tpms") == 0) {
                    menu_focus_index = SubmenuIndexTpmsReader;
                } else if(strcmp(buf, "menu:bruteforcer") == 0) {
                    menu_focus_index = SubmenuIndexSubRATT;
                } else if(strcmp(buf, "menu:keeloq_keys") == 0) {
                    menu_focus_index = SubmenuIndexKeeloqKeys;
                } else if(strcmp(buf, "menu:keeloq_bf") == 0) {
                    menu_focus_index = SubmenuIndexKeeloqBf2;
                } else if(strcmp(buf, "menu:addmanual") == 0) {
                    menu_focus_index = SubmenuIndexAddManuallyAdvanced;
                } else if(strcmp(buf, "menu:rawdual") == 0) {
                    menu_focus_index = SubmenuIndexRawDual;
                } else if(strcmp(buf, "menu:grabber") == 0) {
                    menu_focus_index = SubmenuIndexSignalGrabber;
                } else if(strcmp(buf, "read") == 0) {
                    static const char read_arg[] = "read";
                    p = (void*)read_arg;
                } else if(strcmp(buf, "readraw") == 0) {
                    static const char readraw_arg[] = "readraw";
                    p = (void*)readraw_arg;
                }
            }
            storage_file_close(f);
            storage_file_free(f);
            storage_simply_remove(storage, "/ext/subghz/.focus_menu");

            if(storage_file_exists(storage, "/ext/subghz/.focus_file")) {
                storage_simply_remove(storage, "/ext/subghz/.focus_file");
            }
        } else if(storage_file_exists(storage, "/ext/subghz/.focus_file")) {
            focus_file_existed = true;
            File* f = storage_file_alloc(storage);
            if(storage_file_open(f, "/ext/subghz/.focus_file", FSAM_READ, FSOM_OPEN_EXISTING)) {
                uint16_t read = storage_file_read(f, focus_file_buf, sizeof(focus_file_buf) - 1);
                focus_file_buf[read] = '\0';

                if(read > 0) {
                    const char* prefix = "rawreturn:";
                    const char* km_prefix = "keymethod:";
                    const char* info_prefix = "inforeturn:";
                    if(strncmp(focus_file_buf, prefix, strlen(prefix)) == 0) {

                        p = focus_file_buf + strlen(prefix);
                        focus_file_existed = true;
                    } else if(strncmp(focus_file_buf, info_prefix, strlen(info_prefix)) == 0) {

                        p = focus_file_buf + strlen(info_prefix);
                        is_inforeturn = true;
                    } else if(strncmp(focus_file_buf, km_prefix, strlen(km_prefix)) == 0) {

                        p = focus_file_buf + strlen(km_prefix);
                        is_keymethod_return = true;
                    } else {
                        p = focus_file_buf;
                    }
                }
            }
            storage_file_close(f);
            storage_file_free(f);
            storage_simply_remove(storage, "/ext/subghz/.focus_file");
        }

        if(subghz_add_manual_read_handoff(storage, &add_manual_handoff)) {
            is_add_manual = true;
            storage_simply_remove(storage, SUBGHZ_ADD_MANUAL_HANDOFF_PATH);
        }

        furi_record_close(RECORD_STORAGE);
    }

    bool open_receiver = (p && strcmp((const char*)p, "read") == 0);
    bool open_readraw  = (p && strcmp((const char*)p, "readraw") == 0);

    bool open_menu_focused = (menu_focus_index != 0) && !is_add_manual;

    bool is_rawreturn = (focus_file_existed &&
                         !is_keymethod_return &&
                         p != NULL &&
                         p != (void*)focus_file_buf &&
                         (const char*)p == focus_file_buf + 10);

    bool alloc_for_tx;
    if(p && strlen((const char*)p) && !open_receiver && !open_readraw && !open_menu_focused &&
       !is_rawreturn && !is_keymethod_return && !is_inforeturn) {
        alloc_for_tx = true;
    } else {
        alloc_for_tx = false;
    }

    SubGhz* subghz = subghz_alloc(alloc_for_tx);

    if(!is_ram_recover_pass && !subghz_txrx_is_database_loaded(subghz->txrx)) {
        subghz_ram_recovery_restart();
        subghz_free(subghz, alloc_for_tx);
        return 0;
    }

    if(alloc_for_tx) {
        subghz->raw_send_only = true;
    } else {
        subghz->raw_send_only = false;
    }

    if(open_menu_focused) {
        view_dispatcher_attach_to_gui(
            subghz->view_dispatcher, subghz->gui, ViewDispatcherTypeFullscreen);

        furi_string_set(subghz->file_path, SUBGHZ_APP_FOLDER);
        scene_manager_set_scene_state(subghz->scene_manager, SubGhzSceneStart, menu_focus_index);
        scene_manager_next_scene(subghz->scene_manager, SubGhzSceneStart);
    } else if(open_receiver) {
        view_dispatcher_attach_to_gui(
            subghz->view_dispatcher, subghz->gui, ViewDispatcherTypeFullscreen);
        furi_string_set(subghz->file_path, SUBGHZ_APP_FOLDER);
        if(subghz_txrx_is_database_loaded(subghz->txrx)) {
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneStart);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReceiver);
        } else {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneShowError, SubGhzCustomEventManagerSet);
            furi_string_set(
                subghz->error_str,
                "No SD card or\ndatabase found.\nSome app function\nmay be reduced.");
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneShowError);
        }
    } else if(open_readraw) {

        view_dispatcher_attach_to_gui(
            subghz->view_dispatcher, subghz->gui, ViewDispatcherTypeFullscreen);
        furi_string_set(subghz->file_path, SUBGHZ_APP_FOLDER);
        if(subghz_txrx_is_database_loaded(subghz->txrx)) {
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneStart);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReadRAW);
        } else {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneShowError, SubGhzCustomEventManagerSet);
            furi_string_set(
                subghz->error_str,
                "No SD card or\ndatabase found.\nSome app function\nmay be reduced.");
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneShowError);
        }
    } else if(is_keymethod_return) {
        view_dispatcher_attach_to_gui(
            subghz->view_dispatcher, subghz->gui, ViewDispatcherTypeFullscreen);
        if(subghz_key_load(subghz, p, true)) {
            furi_string_set(subghz->file_path, (const char*)p);
            scene_manager_set_scene_state(
                subghz->scene_manager,
                SubGhzSceneFiatV1KeyMethod,
                SUBGHZ_FIAT_V1_KEYMETHOD_RETURN_SELECT);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneStart);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneFiatV1KeyMethod);
        } else {
            scene_manager_stop(subghz->scene_manager);
            view_dispatcher_stop(subghz->view_dispatcher);
        }
    } else if(p && strlen((const char*)p)) {
        uint32_t rpc_ctx = 0;

        if(sscanf(p, "RPC %lX", &rpc_ctx) == 1) {
            subghz->rpc_ctx = (void*)rpc_ctx;
            rpc_system_app_set_callback(subghz->rpc_ctx, subghz_rpc_command_callback, subghz);
            rpc_system_app_send_started(subghz->rpc_ctx);
            view_dispatcher_attach_to_gui(
                subghz->view_dispatcher, subghz->gui, ViewDispatcherTypeDesktop);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneRpc);
        } else {
            view_dispatcher_attach_to_gui(
                subghz->view_dispatcher, subghz->gui, ViewDispatcherTypeFullscreen);
            if(subghz_key_load(subghz, p, true)) {
                furi_string_set(subghz->file_path, (const char*)p);

                if(subghz_get_load_type_file(subghz) == SubGhzLoadTypeFileRaw) {

                    subghz_rx_key_state_set(subghz, SubGhzRxKeyStateRAWLoad);
                    if(is_rawreturn || is_inforeturn) {

                        scene_manager_set_scene_state(
                            subghz->scene_manager,
                            SubGhzSceneStart,
                            SubmenuIndexReadRAW);
                        scene_manager_next_scene(subghz->scene_manager, SubGhzSceneStart);
                    }
                    scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReadRAW);
                } else {

                    if(is_inforeturn) {
                        subghz_rx_key_state_set(subghz, SubGhzRxKeyStateRAWLoad);
                        scene_manager_set_scene_state(
                            subghz->scene_manager, SubGhzSceneStart, SubmenuIndexSaved);
                        scene_manager_next_scene(subghz->scene_manager, SubGhzSceneStart);
                        scene_manager_next_scene(subghz->scene_manager, SubGhzSceneSavedMenu);
                    } else {
                        scene_manager_next_scene(subghz->scene_manager, SubGhzSceneTransmitter);
                    }
                }
            } else {

                scene_manager_stop(subghz->scene_manager);
                view_dispatcher_stop(subghz->view_dispatcher);
            }
        }
    } else if(is_add_manual) {

        view_dispatcher_attach_to_gui(
            subghz->view_dispatcher, subghz->gui, ViewDispatcherTypeFullscreen);
        furi_string_set(subghz->file_path, SUBGHZ_APP_FOLDER);
        if(subghz_txrx_is_database_loaded(subghz->txrx)) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexAddManuallyAdvanced);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneStart);
            subghz_add_manual_apply(subghz, &add_manual_handoff);
            subghz_scene_set_type_generate_protocol_from_infos(subghz);
        } else {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneShowError, SubGhzCustomEventManagerSet);
            furi_string_set(subghz->error_str, "No SD card or\ndatabase found.");
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneShowError);
        }
    } else {
        view_dispatcher_attach_to_gui(
            subghz->view_dispatcher, subghz->gui, ViewDispatcherTypeFullscreen);
        furi_string_set(subghz->file_path, SUBGHZ_APP_FOLDER);
        if(subghz_txrx_is_database_loaded(subghz->txrx)) {
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneStart);
        } else {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneShowError, SubGhzCustomEventManagerSet);
            furi_string_set(
                subghz->error_str,
                "No SD card or\ndatabase found.\nSome app function\nmay be reduced.");
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneShowError);
        }
    }

    furi_hal_power_suppress_charge_enter();

    view_dispatcher_run(subghz->view_dispatcher);

    furi_hal_power_suppress_charge_exit();

    subghz_free(subghz, alloc_for_tx);

    return 0;
}
