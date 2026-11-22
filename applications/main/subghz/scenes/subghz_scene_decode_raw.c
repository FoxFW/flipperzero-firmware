#include "../subghz_i.h"
#include "../subghz_protocol_filter.h"
#include <lib/subghz/subghz_protocol_registry.h>
#include <string.h>
#include "../helpers/subghz_lowram_guard.h"
#include "../helpers/subghz_decode_plugin_loader.h"
#include <lib/toolbox/strint.h>
#include <toolbox/stream/stream.h>
#include <flipper_format/flipper_format_i.h>

#define TAG "SubGhzDecodeRaw"

#define SAMPLES_TO_READ_PER_TICK 400

#define SUBGHZ_DECODE_RAW_MIN_HEAP 18432u

#define SUBGHZ_FORCE_RECHECK_MAX 8
#define SUBGHZ_RECHECK_WINDOW_US 500000u
static const char* const subghz_force_recheck_names[] = {"Honda V8"};
static uint16_t subghz_recheck_idx[SUBGHZ_FORCE_RECHECK_MAX];
static uint32_t subghz_recheck_time[SUBGHZ_FORCE_RECHECK_MAX];
static uint8_t subghz_recheck_count;
static uint32_t subghz_pass1_us;
static const char* subghz_recheck_proto_name;

static bool subghz_decode_raw_is_force_recheck(const char* name) {
    if(!name) return false;
    for(size_t i = 0; i < sizeof(subghz_force_recheck_names) / sizeof(subghz_force_recheck_names[0]);
        i++) {
        if(strcmp(name, subghz_force_recheck_names[i]) == 0) return true;
    }
    return false;
}

typedef struct {
    SubGhz* subghz;
    SubGhzRadioPreset preset;
    uint32_t cur_us;
    uint8_t action[SUBGHZ_FORCE_RECHECK_MAX];
    uint8_t repl_type[SUBGHZ_FORCE_RECHECK_MAX];
    FlipperFormat* repl_ff[SUBGHZ_FORCE_RECHECK_MAX];
} SubGhzRecheckCtx;

static void subghz_scene_receiver_update_statusbar(void* context) {
    SubGhz* subghz = context;
    FuriString* history_stat_str = furi_string_alloc();
    if(!subghz_history_get_text_space_left(subghz->history, history_stat_str)) {
        FuriString* frequency_str = furi_string_alloc();
        FuriString* modulation_str = furi_string_alloc();

        subghz_txrx_get_frequency_and_modulation(
            subghz->txrx, frequency_str, modulation_str, false);

        subghz_view_receiver_add_data_statusbar(
            subghz->subghz_receiver,
            furi_string_get_cstr(frequency_str),
            furi_string_get_cstr(modulation_str),
            furi_string_get_cstr(history_stat_str),
            subghz_txrx_hopper_get_state(subghz->txrx) != SubGhzHopperStateOFF,
            READ_BIT(subghz->filter, SubGhzProtocolFlag_BinRAW) > 0);

        furi_string_free(frequency_str);
        furi_string_free(modulation_str);
    } else {
        subghz_view_receiver_add_data_statusbar(
            subghz->subghz_receiver,
            furi_string_get_cstr(history_stat_str),
            "",
            "",
            subghz_txrx_hopper_get_state(subghz->txrx) != SubGhzHopperStateOFF,
            READ_BIT(subghz->filter, SubGhzProtocolFlag_BinRAW) > 0);
    }
    furi_string_free(history_stat_str);
}

void subghz_scene_decode_raw_callback(SubGhzCustomEvent event, void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, event);
}

static void subghz_scene_add_to_history_callback(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    SubGhzRadioPreset preset = subghz_txrx_get_preset(subghz->txrx);

    if((decoder_base->protocol->flag & subghz->ignore_filter) != 0) return;

    if(subghz_decode_raw_is_force_recheck(decoder_base->protocol->name)) {
        if(subghz_recheck_count < SUBGHZ_FORCE_RECHECK_MAX) {
            if(subghz_recheck_count == 0)
                subghz_recheck_proto_name = decoder_base->protocol->name;
            subghz_recheck_time[subghz_recheck_count] = subghz_pass1_us;
            subghz_recheck_idx[subghz_recheck_count] = subghz_history_get_item(subghz->history);
            subghz_recheck_count++;
        }
        subghz_receiver_reset(receiver);
        return;
    }

    bool identical_ping = false;
    {
        bool ping_notify = false;
        int16_t ident =
            subghz_history_find_identical(subghz->history, decoder_base, &preset, &ping_notify);
        if(ident >= 0) {
            identical_ping = true;
            subghz_view_receiver_flash_item(subghz->subghz_receiver, (uint16_t)ident);
            if(ping_notify) {
                subghz->state_notifications = SubGhzNotificationStateRxDone;
            }
        }
    }

    if(!identical_ping && subghz_history_add_to_history(
                                 subghz->history, decoder_base, &preset, SubGhzHistorySourceNone)) {

        if(!subghz->last_settings->show_psa_encrypted &&
           subghz_history_last_is_undecoded_psa(subghz->history)) {
            subghz_history_remove_last_if_undecoded_psa(subghz->history);
        } else {
            subghz->state_notifications = SubGhzNotificationStateRxDone;

            subghz_view_receiver_add_item_to_menu(subghz->subghz_receiver);

            subghz_history_set_raw_end_us(
                subghz->history,
                (uint16_t)(subghz_history_get_item(subghz->history) - 1),
                subghz_pass1_us);

            subghz_history_dedup_after_add(subghz, NULL, NULL);

            subghz_scene_receiver_update_statusbar(subghz);
        }
    }
    if(decoder_base->protocol->name && !strcmp(decoder_base->protocol->name, "Hitag-2")) {
        subghz_receiver_reset_except_by_name(receiver, "Fiat V1");
    } else {
        subghz_receiver_reset(receiver);
    }
}

static void subghz_decode_raw_recheck_cb(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    SubGhzRecheckCtx* rc = context;
    if(subghz_decode_raw_is_force_recheck(decoder_base->protocol->name)) {
        return;
    }
    bool notify = false;
    int16_t in_h1 =
        subghz_history_find_identical(rc->subghz->history, decoder_base, &rc->preset, &notify);
    for(uint8_t i = 0; i < subghz_recheck_count; i++) {
        if(rc->action[i] != 0) continue;
        uint32_t dt = (rc->cur_us > subghz_recheck_time[i]) ?
                          (rc->cur_us - subghz_recheck_time[i]) :
                          (subghz_recheck_time[i] - rc->cur_us);
        if(dt > SUBGHZ_RECHECK_WINDOW_US) continue;
        if(in_h1 >= 0) {
            rc->action[i] = 1;
        } else {
            FlipperFormat* ff = flipper_format_string_alloc();
            if(subghz_protocol_decoder_base_serialize(decoder_base, ff, &rc->preset) ==
               SubGhzProtocolStatusOk) {
                rc->repl_ff[i] = ff;
                rc->repl_type[i] = decoder_base->protocol->type;
                rc->action[i] = 2;
            } else {
                flipper_format_free(ff);
                rc->action[i] = 1;
            }
        }
        break;
    }
    subghz_receiver_reset(receiver);
}

static void subghz_decode_raw_run_recheck(SubGhz* subghz) {
    if(subghz_recheck_count == 0) return;
    subghz_recheck_cover_show(subghz, subghz_recheck_proto_name);
    furi_delay_ms(1);
    FuriLogLevel prev_log_level = furi_log_get_level();
    furi_log_set_level(FuriLogLevelInfo);
    FURI_LOG_I(TAG, "recheck A: start triggers=%d", subghz_recheck_count);
    SubGhzReceiver* receiver = subghz_txrx_get_receiver(subghz->txrx);
    SubGhzRecheckCtx rc = {
        .subghz = subghz, .preset = subghz_txrx_get_preset(subghz->txrx), .cur_us = 0};
    subghz_receiver_set_rx_callback(receiver, subghz_decode_raw_recheck_cb, &rc);
    subghz_receiver_reset(receiver);
    subghz_receiver_reset_decode_result(receiver);
    FURI_LOG_I(TAG, "recheck B: opening %s", furi_string_get_cstr(subghz->decode_raw_source));

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    FuriString* line = furi_string_alloc();
    bool recheck_opened =
        flipper_format_file_open_existing(ff, furi_string_get_cstr(subghz->decode_raw_source));
    FURI_LOG_I(TAG, "recheck C: opened=%d feeding", recheck_opened);
    if(recheck_opened) {
        Stream* stream = flipper_format_get_raw_stream(ff);
        uint32_t yield_ctr = 0;
        while(stream_read_line(stream, line)) {
            const char* raw = strstr(furi_string_get_cstr(line), "RAW_Data: ");
            if(!raw) continue;
            char* ppos = (char*)strchr(raw, ' ');
            int32_t dur;
            while(ppos && strint_to_int32(ppos, &ppos, &dur, 10) == StrintParseNoError) {
                if(dur == 0 || dur < -1000000 || dur > 1000000) continue;
                rc.cur_us += (uint32_t)(dur > 0 ? dur : -dur);
                subghz_receiver_decode(receiver, dur > 0, (uint32_t)(dur > 0 ? dur : -dur));
                if((++yield_ctr & 0x1FFu) == 0) furi_delay_ms(1);
            }
        }
    }
    FURI_LOG_I(TAG, "recheck D: fed cur_us=%lu", (unsigned long)rc.cur_us);
    furi_string_free(line);
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);

    subghz_receiver_reset(receiver);
    subghz_txrx_set_rx_callback(subghz->txrx, subghz_scene_add_to_history_callback, subghz);
    FURI_LOG_I(TAG, "recheck E: restored");

    bool changed = false;
    for(int8_t i = (int8_t)subghz_recheck_count - 1; i >= 0; i--) {
        if(rc.action[i] == 2 && rc.repl_ff[i]) {

            if(subghz_history_insert_item(
                   subghz->history,
                   subghz_recheck_idx[i],
                   rc.repl_ff[i],
                   rc.repl_type[i],
                   &rc.preset)) {

                subghz_history_set_raw_end_us(
                    subghz->history, subghz_recheck_idx[i], subghz_recheck_time[i]);
                changed = true;
            } else {
                flipper_format_free(rc.repl_ff[i]);
            }
        }

    }
    FURI_LOG_I(TAG, "recheck E2: changed=%d", changed);

    if(changed) {
        subghz_view_receiver_set_history_item_count(
            subghz->subghz_receiver, subghz_history_get_item(subghz->history));
        subghz_scene_receiver_update_statusbar(subghz);
    }
    subghz_recheck_cover_hide(subghz);
    FURI_LOG_I(TAG, "recheck F: done");
    furi_log_set_level(prev_log_level);
    subghz_recheck_count = 0;
}

bool subghz_scene_decode_raw_start(SubGhz* subghz) {
    FuriString* file_name = furi_string_alloc();
    bool success = false;
    do {
        if(!flipper_format_rewind(subghz_txrx_get_fff_data(subghz->txrx))) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }

        if(!flipper_format_read_string(
               subghz_txrx_get_fff_data(subghz->txrx), "File_name", file_name)) {
            FURI_LOG_E(TAG, "Missing File_name");
            break;
        }

        furi_string_set(subghz->decode_raw_source, file_name);
        success = true;
    } while(false);

    if(success) {

        subghz->decode_raw_file_worker_encoder = subghz_file_encoder_worker_alloc();
        if(subghz_file_encoder_worker_start(
               subghz->decode_raw_file_worker_encoder,
               furi_string_get_cstr(file_name),
               subghz_txrx_radio_device_get_name(subghz->txrx))) {

            furi_delay_ms(100);
        } else {
            success = false;
        }

        if(!success) {
            subghz_file_encoder_worker_free(subghz->decode_raw_file_worker_encoder);
        }
    }

    furi_string_free(file_name);
    return success;
}

bool subghz_scene_decode_raw_next(SubGhz* subghz) {
    LevelDuration level_duration;
    SubGhzReceiver* receiver = subghz_txrx_get_receiver(subghz->txrx);
    for(uint32_t read = SAMPLES_TO_READ_PER_TICK; read > 0; --read) {
        level_duration =
            subghz_file_encoder_worker_get_level_duration(subghz->decode_raw_file_worker_encoder);
        if(!level_duration_is_reset(level_duration)) {
            if(level_duration_is_wait(level_duration)) {
                FURI_LOG_W(TAG, "LD tells wait!");
                return true;
            }
            bool level = level_duration_get_level(level_duration);
            uint32_t duration = level_duration_get_duration(level_duration);
            if(duration > 1000000) {
                FURI_LOG_E(TAG, "LD came with overflow: %ld", duration);
                return true;
            }
            subghz_pass1_us += duration;
            subghz_receiver_decode(receiver, level, duration);
        } else {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneDecodeRAW, SubGhzDecodeRawStateLoaded);
            subghz->state_notifications = SubGhzNotificationStateIDLE;

            if(subghz_history_get_item(subghz->history) == 0) {

                extern void subghz_scene_decode_raw_failed_set_context(bool all_protocols_enabled);
                size_t total = subghz_protocol_registry_count(&subghz_protocol_registry);
                size_t enabled = subghz_protocol_filter_enabled_count(subghz->protocol_filter, total);
                subghz_scene_decode_raw_failed_set_context(enabled == total);
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneDecodeRawFailed);
            } else {
                if(subghz->decode_raw_file_worker_encoder &&
                   subghz_file_encoder_worker_is_running(
                       subghz->decode_raw_file_worker_encoder)) {
                    subghz_file_encoder_worker_stop(subghz->decode_raw_file_worker_encoder);
                }
                subghz_decode_raw_run_recheck(subghz);

                {
                    bool drop_even_if_alone = !subghz->last_settings->show_psa_encrypted;
                    int16_t psa_rm;
                    while((psa_rm = subghz_history_remove_one_undecoded_psa_extra(
                               subghz->history, drop_even_if_alone)) >= 0) {
                        subghz_view_receiver_disable_draw_callback(subghz->subghz_receiver);
                        subghz_view_receiver_delete_item(
                            subghz->subghz_receiver, (uint16_t)psa_rm);
                        subghz_view_receiver_enable_draw_callback(subghz->subghz_receiver);
                    }
                    subghz_scene_receiver_update_statusbar(subghz);
                }
                subghz_view_receiver_add_data_progress(subghz->subghz_receiver, "Done!");
            }
            return false;
        }
    }

    FuriString* progress_str = furi_string_alloc();
    subghz_file_encoder_worker_get_text_progress(
        subghz->decode_raw_file_worker_encoder, progress_str);

    subghz_view_receiver_add_data_progress(
        subghz->subghz_receiver, furi_string_get_cstr(progress_str));

    furi_string_free(progress_str);

    return true;
}

void subghz_scene_decode_raw_on_enter(void* context) {
    SubGhz* subghz = context;
    subghz_decode_plugin_loader_flush();
    subghz_lowram_mitigate(
        SUBGHZ_LOWRAM_SOFTLOCK_THRESHOLD + subghz_txrx_receiver_heap_needed(subghz->txrx));
    subghz_history_release_cache(subghz->history);
    subghz_history_set_combine_repeats(subghz->history, subghz->last_settings->combine_repeats);

    subghz_view_receiver_set_mode(subghz->subghz_receiver, SubGhzViewReceiverModeFile);
    subghz_view_receiver_set_callback(
        subghz->subghz_receiver, subghz_scene_decode_raw_callback, subghz);

    subghz_txrx_set_rx_callback(subghz->txrx, subghz_scene_add_to_history_callback, subghz);

    subghz_txrx_receiver_set_filter(subghz->txrx, SubGhzProtocolFlag_Decodable);
    subghz_txrx_receiver_apply_modulation_filter(subghz->txrx);

    subghz->ignore_filter = subghz->last_settings->ignore_filter;

    if(scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneDecodeRAW) ==
       SubGhzDecodeRawStateStart) {

        if(memmgr_get_free_heap() < SUBGHZ_DECODE_RAW_MIN_HEAP) {

            subghz->decode_raw_file_worker_encoder = NULL;
            subghz_view_receiver_add_data_progress(
                subghz->subghz_receiver, "RAM too low");
        } else {

            subghz_receiver_reset_decode_result(subghz_txrx_get_receiver(subghz->txrx));
            subghz_history_reset(subghz->history);
            subghz_recheck_count = 0;
            subghz_pass1_us = 0;
            subghz_recheck_proto_name = NULL;
            if(subghz_scene_decode_raw_start(subghz)) {
                scene_manager_set_scene_state(
                    subghz->scene_manager, SubGhzSceneDecodeRAW, SubGhzDecodeRawStateLoading);
                subghz->state_notifications = SubGhzNotificationStateRx;
            }
        }
    } else {

        subghz_view_receiver_exit(subghz->subghz_receiver);
        for(uint16_t i = 0; i < subghz_history_get_item(subghz->history); i++) {
            subghz_view_receiver_add_item_to_menu(subghz->subghz_receiver);
        }
        subghz_view_receiver_set_idx_menu(subghz->subghz_receiver, subghz->idx_menu_chosen);
    }

    subghz_scene_receiver_update_statusbar(subghz);

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdReceiver);
}

bool subghz_scene_decode_raw_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case SubGhzCustomEventViewReceiverBack:
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneDecodeRAW, SubGhzDecodeRawStateStart);
            subghz->idx_menu_chosen = 0;

            subghz_txrx_set_rx_callback(subghz->txrx, NULL, subghz);

            if(subghz->decode_raw_file_worker_encoder) {
                if(subghz_file_encoder_worker_is_running(
                       subghz->decode_raw_file_worker_encoder)) {
                    subghz_file_encoder_worker_stop(subghz->decode_raw_file_worker_encoder);
                }
                subghz_file_encoder_worker_free(subghz->decode_raw_file_worker_encoder);
                subghz->decode_raw_file_worker_encoder = NULL;
            }

            subghz->state_notifications = SubGhzNotificationStateIDLE;

            subghz_rx_key_state_set(subghz, SubGhzRxKeyStateRAWLoad);
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneReadRAW, SubGhzCustomEventManagerNoSet);
            scene_manager_search_and_switch_to_previous_scene(
                subghz->scene_manager, SubGhzSceneMoreRAW);
            consumed = true;
            break;
        case SubGhzCustomEventViewReceiverOK:
            subghz->idx_menu_chosen = subghz_view_receiver_get_idx_menu(subghz->subghz_receiver);
            subghz->state_notifications = SubGhzNotificationStateIDLE;
            subghz_txrx_set_rx_callback(subghz->txrx, NULL, subghz);
            if(subghz->decode_raw_file_worker_encoder &&
               subghz_file_encoder_worker_is_running(
                   subghz->decode_raw_file_worker_encoder)) {
                subghz_file_encoder_worker_stop(subghz->decode_raw_file_worker_encoder);
            }
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReceiverInfo);
            consumed = true;
            break;
        case SubGhzCustomEventViewReceiverConfig:
            FURI_LOG_W(TAG, "No config options");
            consumed = true;
            break;
        case SubGhzCustomEventViewReceiverOffDisplay:
            notification_message(subghz->notifications, &sequence_display_backlight_off);
            consumed = true;
            break;
        default:
            break;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        subghz_history_flush(subghz->history);
        switch(subghz->state_notifications) {
        case SubGhzNotificationStateRx:
            notification_message(subghz->notifications, &sequence_blink_cyan_10);
            break;
        case SubGhzNotificationStateRxDone:
            if(subghz_txrx_speaker_get_state(subghz->txrx) == SubGhzSpeakerStateEnable) {
                notification_message(subghz->notifications, &subghz_sequence_rx);
            } else {
                notification_message(subghz->notifications, &subghz_sequence_rx_nosound);
            }
            subghz->state_notifications = SubGhzNotificationStateRx;
            break;
        default:
            break;
        }

        switch(scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneDecodeRAW)) {
        case SubGhzDecodeRawStateLoading:
            subghz_scene_decode_raw_next(subghz);
            break;
        default:
            break;
        }
    }
    return consumed;
}

void subghz_scene_decode_raw_on_exit(void* context) {
    UNUSED(context);
    subghz_lowram_softlock_release();
}
