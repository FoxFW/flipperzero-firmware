#include "../subghz_i.h"
#include "../views/subghz_read_raw.h"
#include <lib/subghz/protocols/raw.h>
#include <toolbox/path.h>
#include <stdlib.h>
#include <string.h>
#include "../helpers/subghz_lowram_guard.h"

#define RAW_FILE_NAME "RAW_"
#define TAG           "SubGhzSceneReadRAW"

#define SUBGHZ_AUTO_START_COUNTDOWN_SEC 3
static uint8_t s_start_countdown_sec = 0;
static uint8_t s_start_subtick = 0;
static bool s_start_countdown_fired = false;

static bool s_start_cancelled = false;

bool subghz_scene_read_raw_update_filename(SubGhz* subghz) {
    bool ret = false;

    FuriString* temp_str = furi_string_alloc();
    do {
        FlipperFormat* fff_data = subghz_txrx_get_fff_data(subghz->txrx);
        if(!flipper_format_rewind(fff_data)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }

        if(!flipper_format_read_string(fff_data, "File_name", temp_str)) {
            FURI_LOG_E(TAG, "Missing File_name");
            break;
        }

        furi_string_set(subghz->file_path, temp_str);

        ret = true;
    } while(false);

    furi_string_free(temp_str);
    return ret;
}

static void subghz_scene_read_raw_update_statusbar(void* context) {
    furi_assert(context);
    SubGhz* subghz = context;

    FuriString* frequency_str = furi_string_alloc();
    FuriString* modulation_str = furi_string_alloc();

#ifdef SUBGHZ_EXT_PRESET_NAME
    subghz_txrx_get_frequency_and_modulation(subghz->txrx, frequency_str, modulation_str, true);
#else
    subghz_txrx_get_frequency_and_modulation(subghz->txrx, frequency_str, modulation_str, false);
#endif
    subghz_read_raw_add_data_statusbar(
        subghz->subghz_read_raw,
        furi_string_get_cstr(frequency_str),
        furi_string_get_cstr(modulation_str));

    furi_string_free(frequency_str);
    furi_string_free(modulation_str);

    subghz_read_raw_set_radio_device_type(
        subghz->subghz_read_raw, subghz_txrx_radio_device_get(subghz->txrx));
}

void subghz_scene_read_raw_callback(SubGhzCustomEvent event, void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, event);
}

void subghz_scene_read_raw_callback_end_tx(void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(
        subghz->view_dispatcher, SubGhzCustomEventViewReadRAWSendStop);
}

#define ENVELOPE_PREVIEW_SAMPLES 2048u

typedef struct {
    File*  file;
    uint8_t buf[128];
    size_t len;
    size_t pos;
    bool   eof;
} SubghzRawLineReader;

static bool subghz_raw_lr_read_line(SubghzRawLineReader* lr, FuriString* out) {
    furi_string_reset(out);
    bool any = false;
    while(true) {
        if(lr->pos >= lr->len) {
            if(lr->eof) break;
            lr->len = storage_file_read(lr->file, lr->buf, sizeof(lr->buf));
            lr->pos = 0;
            if(lr->len == 0) {
                lr->eof = true;
                break;
            }
        }
        char c = (char)lr->buf[lr->pos++];
        if(c == '\n') {
            any = true;
            break;
        }
        if(c == '\r') continue;
        furi_string_push_back(out, c);
        any = true;
    }
    return any;
}

static const uint8_t ENVELOPE_ZOOM_WINDOW_PCT[5] = {100, 60, 36, 22, 13};

static void subghz_scene_read_raw_load_envelope_ex(
    SubGhz* subghz, uint8_t zoom_level, uint8_t center_pct, uint64_t* out_total_us) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File*    file     = storage_file_alloc(storage);

    uint8_t  preview[100];
    memset(preview, 0, sizeof(preview));
    uint32_t total_samples = 0;
    bool     ok            = false;
    uint64_t total_us      = 0;
    if(out_total_us) *out_total_us = 0;

    int32_t* buf = malloc(sizeof(int32_t) * ENVELOPE_PREVIEW_SAMPLES);

    if(buf && storage_file_open(
                  file, furi_string_get_cstr(subghz->file_path),
                  FSAM_READ, FSOM_OPEN_EXISTING)) {

        SubghzRawLineReader lr = {.file = file, .len = 0, .pos = 0, .eof = false};
        FuriString* line = furi_string_alloc();

        while(subghz_raw_lr_read_line(&lr, line)) {
            const char* s = furi_string_get_cstr(line);
            if(strncmp(s, "RAW_Data:", 9) == 0) {
                const char* p = s + 9;
                char* end;
                while(*p) {
                    long v = strtol(p, &end, 10);
                    if(end == p) {
                        if(*p == '\0') break;
                        p++;
                        continue;
                    }
                    p = end;

                    total_us += (uint64_t)(v < 0 ? -(int64_t)v : (int64_t)v);

                    if(total_samples < ENVELOPE_PREVIEW_SAMPLES)
                        buf[total_samples++] = (int32_t)v;
                }
            }
        }

        furi_string_free(line);
        storage_file_close(file);

        if(total_samples > 0) {

            if(out_total_us) *out_total_us = total_us;

            if(total_us > 0) {

                uint64_t disp_start = 0;
                uint64_t disp_end   = total_us;
                if(zoom_level > 0 && zoom_level < 5) {
                    uint64_t window_span =
                        (total_us * ENVELOPE_ZOOM_WINDOW_PCT[zoom_level]) / 100;
                    if(window_span < 1) window_span = 1;
                    uint64_t center = ((uint64_t)center_pct * total_us) / 100;
                    uint64_t half   = window_span / 2;
                    disp_start = (center > half) ? (center - half) : 0;
                    disp_end   = disp_start + window_span;
                    if(disp_end > total_us) {
                        disp_end   = total_us;
                        disp_start = (disp_end > window_span) ? (disp_end - window_span) : 0;
                    }
                }
                uint64_t disp_span = (disp_end > disp_start) ? (disp_end - disp_start) : 1;

                uint32_t bucket_on_us[100] = {0};
                uint64_t current_us = 0;

                for(uint32_t i = 0; i < total_samples; i++) {
                    int32_t  val = buf[i];
                    uint64_t dur = (uint64_t)(val < 0 ? -val : val);
                    bool     hi  = (val > 0);
                    uint64_t seg_start = current_us;
                    uint64_t seg_end   = current_us + dur;
                    current_us = seg_end;

                    if(!hi || dur == 0) continue;

                    uint64_t cs = seg_start > disp_start ? seg_start : disp_start;
                    uint64_t ce = seg_end   < disp_end   ? seg_end   : disp_end;
                    if(ce <= cs) continue;

                    uint8_t b0 = (uint8_t)(((cs - disp_start) * 100) / disp_span);
                    uint8_t b1 = (uint8_t)(((ce - disp_start) * 100) / disp_span);
                    if(b1 > 99) b1 = 99;
                    for(uint8_t b = b0; b <= b1; b++) {
                        uint64_t bstart = disp_start + ((uint64_t)b * disp_span / 100);
                        uint64_t bend   = disp_start + ((uint64_t)(b + 1) * disp_span / 100);
                        uint64_t ov_s   = cs > bstart ? cs : bstart;
                        uint64_t ov_e   = ce < bend   ? ce : bend;
                        if(ov_e > ov_s) bucket_on_us[b] += (uint32_t)(ov_e - ov_s);
                    }
                }

                for(uint8_t b = 0; b < 100; b++) {
                    uint64_t bstart = disp_start + ((uint64_t)b * disp_span / 100);
                    uint64_t bend   = disp_start + ((uint64_t)(b + 1) * disp_span / 100);
                    uint64_t span   = (bend > bstart) ? (bend - bstart) : 1;
                    uint32_t inten  = (uint32_t)((uint64_t)bucket_on_us[b] * 255 / span);
                    preview[b] = (uint8_t)(inten > 255 ? 255 : inten);
                }
                ok = true;
            }
        }
    } else if(file) {
        storage_file_close(file);
    }

    if(buf) free(buf);
    if(file) storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    subghz_read_raw_set_envelope(
        subghz->subghz_read_raw,
        ok ? preview : NULL,
        (uint32_t)total_us);
}

static void subghz_scene_read_raw_load_envelope(SubGhz* subghz) {
    subghz_scene_read_raw_load_envelope_ex(subghz, 0, 0, NULL);
}

static void subghz_scene_read_raw_load_envelope_zoomed(
    SubGhz* subghz, uint8_t zoom_level, uint8_t center_pct) {
    subghz_scene_read_raw_load_envelope_ex(subghz, zoom_level, center_pct, NULL);
}

void subghz_scene_read_raw_on_enter(void* context) {
    SubGhz* subghz = context;
    FuriString* file_name = furi_string_alloc();

    float threshold_rssi = subghz_threshold_rssi_get(subghz->threshold_rssi);
    switch(subghz_rx_key_state_get(subghz)) {
    case SubGhzRxKeyStateBack:
        subghz_read_raw_set_status(
            subghz->subghz_read_raw, SubGhzReadRAWStatusIDLE, "", threshold_rssi);
        break;
    case SubGhzRxKeyStateRAWLoad:
    case SubGhzRxKeyStateRAWMore:
        path_extract_filename(subghz->file_path, file_name, true);
        subghz_read_raw_set_status(
            subghz->subghz_read_raw,
            SubGhzReadRAWStatusLoadKeyTX,
            furi_string_get_cstr(file_name),
            threshold_rssi);

        subghz_read_raw_set_allow_new(subghz->subghz_read_raw, false);

        {
            uint8_t zoom = (uint8_t)subghz->last_settings->raw_playback_zoom_level;
            subghz_read_raw_set_zoom_level(subghz->subghz_read_raw, zoom);
            if(zoom > 0) {
                subghz_scene_read_raw_load_envelope_zoomed(subghz, zoom, 0);
            } else {
                subghz_scene_read_raw_load_envelope(subghz);
            }
        }
        break;
    case SubGhzRxKeyStateRAWSave:
        path_extract_filename(subghz->file_path, file_name, true);
        subghz_read_raw_set_status(
            subghz->subghz_read_raw,
            SubGhzReadRAWStatusSaveKey,
            furi_string_get_cstr(file_name),
            threshold_rssi);

        subghz_read_raw_set_allow_new(subghz->subghz_read_raw, true);
        break;
    default:
        subghz_read_raw_set_status(
            subghz->subghz_read_raw, SubGhzReadRAWStatusStart, "", threshold_rssi);
        s_start_countdown_sec = SUBGHZ_AUTO_START_COUNTDOWN_SEC;
        s_start_subtick = 0;
        s_start_countdown_fired = false;
        s_start_cancelled = false;
        subghz_read_raw_set_start_countdown(subghz->subghz_read_raw, s_start_countdown_sec);
        break;
    }

    if((subghz_rx_key_state_get(subghz) != SubGhzRxKeyStateBack) &&
       (subghz_rx_key_state_get(subghz) != SubGhzRxKeyStateRAWLoad)) {
        subghz_rx_key_state_set(subghz, SubGhzRxKeyStateIDLE);

        if(furi_string_empty(file_name)) {
            subghz_txrx_set_preset_internal(
                subghz->txrx,
                subghz->last_settings->frequency,
                subghz->last_settings->preset_index,
                subghz->last_settings->tx_power);
        }
    }
    subghz_scene_read_raw_update_statusbar(subghz);

    subghz_read_raw_set_callback(subghz->subghz_read_raw, subghz_scene_read_raw_callback, subghz);

    subghz_read_raw_set_viz_mode(
        subghz->subghz_read_raw,
        (SubGhzReadRawVizMode)subghz->last_settings->visualizer_display_mode);

    furi_check(subghz_txrx_load_decoder_by_name_protocol(subghz->txrx, SUBGHZ_PROTOCOL_RAW_NAME));

    subghz_txrx_receiver_set_filter(subghz->txrx, SubGhzProtocolFlag_RAW);
    furi_string_free(file_name);

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdReadRAW);
}

bool subghz_scene_read_raw_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    bool consumed = false;
    SubGhzProtocolDecoderRAW* decoder_raw =
        (SubGhzProtocolDecoderRAW*)subghz_txrx_get_decoder(subghz->txrx);
    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case SubGhzCustomEventViewReadRAWBack:

            if(subghz_read_raw_get_status(subghz->subghz_read_raw) == SubGhzReadRAWStatusStart &&
               s_start_countdown_sec > 0) {

                s_start_countdown_sec = 0;
                s_start_cancelled = true;
                subghz_read_raw_set_start_countdown(subghz->subghz_read_raw, 0);
                consumed = true;
                break;
            }

            subghz_txrx_stop(subghz->txrx);

            subghz_protocol_raw_save_to_file_stop(decoder_raw);
            subghz->state_notifications = SubGhzNotificationStateIDLE;

            if((subghz_rx_key_state_get(subghz) == SubGhzRxKeyStateAddKey) ||
               (subghz_rx_key_state_get(subghz) == SubGhzRxKeyStateBack)) {
                subghz_rx_key_state_set(subghz, SubGhzRxKeyStateExit);
                if(subghz_scene_read_raw_update_filename(subghz)) {
                    furi_string_set(subghz->file_path_tmp, subghz->file_path);
                } else {
                    furi_string_reset(subghz->file_path_tmp);
                }
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneNeedSaving);
            } else {

                if(subghz->raw_send_only) {
                    subghz_txrx_set_default_preset(subghz->txrx, 0);
                } else {
                    subghz_txrx_set_default_preset(subghz->txrx, subghz->last_settings->frequency);
                }
                if(!scene_manager_search_and_switch_to_previous_scene(
                       subghz->scene_manager, SubGhzSceneSaved)) {
                    if(!scene_manager_search_and_switch_to_previous_scene(
                           subghz->scene_manager, SubGhzSceneStart)) {
                        scene_manager_stop(subghz->scene_manager);
                        view_dispatcher_stop(subghz->view_dispatcher);
                    }
                }
            }
            consumed = true;
            break;

        case SubGhzCustomEventViewReadRAWTXPause:

            subghz_txrx_stop(subghz->txrx);
            subghz->state_notifications = SubGhzNotificationStateIDLE;
            notification_message(subghz->notifications, &sequence_reset_rgb);
            consumed = true;
            break;

        case SubGhzCustomEventViewReadRAWTXResume: {

            FlipperFormat* fff = subghz_txrx_get_fff_data(subghz->txrx);
            if(fff) {
                flipper_format_rewind(fff);
                if(subghz_tx_start(subghz, fff)) {
                    subghz->state_notifications = SubGhzNotificationStateTx;
                }
            }
            consumed = true;
            break;
        }

        case SubGhzCustomEventViewReadRAWZoomIn:
        case SubGhzCustomEventViewReadRAWZoomOut: {

            uint8_t zoom = subghz_read_raw_get_zoom_level(subghz->subghz_read_raw);
            uint8_t center = subghz_read_raw_get_seek_pct(subghz->subghz_read_raw);
            if(zoom > 0) {
                subghz_scene_read_raw_load_envelope_zoomed(subghz, zoom, center);
            } else {
                subghz_scene_read_raw_load_envelope(subghz);
            }
            subghz->last_settings->raw_playback_zoom_level = zoom;
            subghz_last_settings_save(subghz->last_settings);
            consumed = true;
            break;
        }

        case SubGhzCustomEventViewReadRAWTXRXStop:
            subghz_txrx_stop(subghz->txrx);
            subghz->state_notifications = SubGhzNotificationStateIDLE;
            consumed = true;
            break;

        case SubGhzCustomEventViewReadRAWConfig:
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneReadRAW, SubGhzCustomEventManagerSet);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReceiverConfig);
            consumed = true;
            break;

        case SubGhzCustomEventViewReadRAWErase:
            if((subghz_rx_key_state_get(subghz) == SubGhzRxKeyStateAddKey) ||
               (subghz_rx_key_state_get(subghz) == SubGhzRxKeyStateBack)) {
                if(subghz_scene_read_raw_update_filename(subghz)) {
                    furi_string_set(subghz->file_path_tmp, subghz->file_path);
                    subghz_delete_file(subghz);
                }
            }
            subghz_rx_key_state_set(subghz, SubGhzRxKeyStateIDLE);
            notification_message(subghz->notifications, &sequence_reset_rgb);

            s_start_countdown_sec = SUBGHZ_AUTO_START_COUNTDOWN_SEC;
            s_start_subtick = 0;
            s_start_countdown_fired = false;
            s_start_cancelled = false;
            subghz_read_raw_set_start_countdown(subghz->subghz_read_raw, s_start_countdown_sec);
            consumed = true;
            break;

        case SubGhzCustomEventViewReadRAWMore:
            if(subghz_file_available(subghz)) {
                if(subghz_scene_read_raw_update_filename(subghz)) {
                    scene_manager_set_scene_state(
                        subghz->scene_manager, SubGhzSceneReadRAW, SubGhzCustomEventManagerSet);
                    if(subghz_rx_key_state_get(subghz) != SubGhzRxKeyStateRAWLoad) {
                        subghz_rx_key_state_set(subghz, SubGhzRxKeyStateRAWMore);
                    }
                    scene_manager_next_scene(subghz->scene_manager, SubGhzSceneMoreRAW);
                    consumed = true;
                } else {
                    furi_crash("SubGhz: RAW file name update error.");
                }
            } else {
                if(!scene_manager_search_and_switch_to_previous_scene(
                       subghz->scene_manager, SubGhzSceneStart)) {
                    scene_manager_stop(subghz->scene_manager);
                    view_dispatcher_stop(subghz->view_dispatcher);
                }
            }
            break;

        case SubGhzCustomEventViewReadRAWSendStart:

            if(subghz_file_available(subghz) && subghz_scene_read_raw_update_filename(subghz)) {

                subghz->state_notifications = SubGhzNotificationStateIDLE;
                if(!subghz_tx_start(subghz, subghz_txrx_get_fff_data(subghz->txrx))) {
                    subghz_rx_key_state_set(subghz, SubGhzRxKeyStateBack);
                    subghz_read_raw_set_status(
                        subghz->subghz_read_raw,
                        SubGhzReadRAWStatusIDLE,
                        "",
                        subghz_threshold_rssi_get(subghz->threshold_rssi));
                } else {
                    subghz_txrx_set_raw_file_encoder_worker_callback_end(
                        subghz->txrx, subghz_scene_read_raw_callback_end_tx, subghz);
                    subghz->state_notifications = SubGhzNotificationStateTx;
                }
            } else {
                if(!scene_manager_search_and_switch_to_previous_scene(
                       subghz->scene_manager, SubGhzSceneStart)) {
                    scene_manager_stop(subghz->scene_manager);
                    view_dispatcher_stop(subghz->view_dispatcher);
                }
            }
            consumed = true;
            break;

        case SubGhzCustomEventViewReadRAWSendStop:
            subghz->state_notifications = SubGhzNotificationStateIDLE;
            subghz_txrx_stop(subghz->txrx);
            subghz_read_raw_stop_send(subghz->subghz_read_raw);
            consumed = true;
            break;

        case SubGhzCustomEventViewReadRAWIDLE:
            subghz_txrx_stop(subghz->txrx);
            size_t spl_count = subghz_protocol_raw_get_sample_write(decoder_raw);

            subghz_protocol_raw_save_to_file_stop(decoder_raw);

            FuriString* temp_str = furi_string_alloc();
            furi_string_printf(
                temp_str,
                "%s/%s%s",
                SUBGHZ_RAW_FOLDER,
                RAW_FILE_NAME,
                SUBGHZ_APP_FILENAME_EXTENSION);

            {

                uint32_t rec_ticks  = subghz_read_raw_get_recording_ticks(subghz->subghz_read_raw);
                uint32_t expected_us = (rec_ticks > 0 ? rec_ticks : 1u) * 100000u;

                if(expected_us > 0) {
                    Storage* pad_storage  = furi_record_open(RECORD_STORAGE);
                    File*    pad_file     = storage_file_alloc(pad_storage);

                    if(storage_file_open(
                           pad_file,
                           furi_string_get_cstr(temp_str),
                           FSAM_READ_WRITE,
                           FSOM_OPEN_EXISTING)) {

                        uint64_t fsz = storage_file_size(pad_file);

                        bool has_raw_data = false;
                        if(fsz >= 10u) {
                            uint8_t  scan_buf[256];
                            storage_file_seek(pad_file, 0, true);
                            uint16_t n_read = storage_file_read(pad_file, scan_buf, sizeof(scan_buf) - 1);
                            scan_buf[n_read] = '\0';

                            uint64_t remaining = fsz;
                            has_raw_data = (strstr((char*)scan_buf, "RAW_Data") != NULL);
                            if(!has_raw_data && remaining > (uint64_t)n_read) {

                                while(remaining > (uint64_t)n_read && !has_raw_data) {
                                    n_read = storage_file_read(pad_file, scan_buf, sizeof(scan_buf) - 1);
                                    if(n_read == 0) break;
                                    scan_buf[n_read] = '\0';
                                    has_raw_data = (strstr((char*)scan_buf, "RAW_Data") != NULL);
                                }
                            }
                        }

                        uint32_t rem = expected_us;

                        const uint32_t SILENCE_CHUNK = 999999u;
                        char  buf[32];
                        int   wn;
                        bool  first_chunk = true;
                        uint32_t rem2 = rem;

                        if(has_raw_data) {
                            storage_file_seek(pad_file, (uint32_t)(fsz - 1u), true);
                        } else {
                            storage_file_seek(pad_file, (uint32_t)fsz, true);
                        }

                        while(rem2 > 0) {
                            uint32_t chunk = (rem2 > SILENCE_CHUNK) ? SILENCE_CHUNK : rem2;
                            uint32_t sil   = (chunk > 1u) ? chunk - 1u : 0u;
                            if(first_chunk) {
                                if(has_raw_data) {
                                    wn = sil ? snprintf(buf, sizeof(buf), " 1 -%lu", (unsigned long)sil)
                                             : snprintf(buf, sizeof(buf), " 1");
                                } else {
                                    wn = sil ? snprintf(buf, sizeof(buf), "RAW_Data: 1 -%lu", (unsigned long)sil)
                                             : snprintf(buf, sizeof(buf), "RAW_Data: 1");
                                }
                                first_chunk = false;
                            } else {
                                wn = sil ? snprintf(buf, sizeof(buf), " 1 -%lu", (unsigned long)sil)
                                         : snprintf(buf, sizeof(buf), " 1");
                            }
                            if(wn > 0) storage_file_write(pad_file, buf, (size_t)wn);
                            rem2 -= chunk;
                        }

                        storage_file_write(pad_file, " 1\n", 3u);
                    }

                    storage_file_close(pad_file);
                    storage_file_free(pad_file);
                    furi_record_close(RECORD_STORAGE);
                }
            }
            subghz_protocol_raw_gen_fff_data(
                subghz_txrx_get_fff_data(subghz->txrx),
                furi_string_get_cstr(temp_str),
                subghz_txrx_radio_device_get_name(subghz->txrx));

            furi_string_set(subghz->file_path, temp_str);
            furi_string_free(temp_str);

            subghz_scene_read_raw_load_envelope(subghz);

            if(spl_count > 0) {
                notification_message(subghz->notifications, &sequence_set_green_255);
            } else {
                notification_message(subghz->notifications, &sequence_reset_rgb);
            }

            subghz->state_notifications = SubGhzNotificationStateIDLE;
            subghz_rx_key_state_set(subghz, SubGhzRxKeyStateAddKey);

            consumed = true;
            break;

        case SubGhzCustomEventViewReadRAWREC:
            if(subghz_rx_key_state_get(subghz) != SubGhzRxKeyStateIDLE) {
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneNeedSaving);
            } else {
                SubGhzRadioPreset preset = subghz_txrx_get_preset(subghz->txrx);
                if(subghz_protocol_raw_save_to_file_init(decoder_raw, RAW_FILE_NAME, &preset)) {
                    subghz_txrx_rx_start(subghz->txrx);
                    subghz->state_notifications = SubGhzNotificationStateRx;

                    subghz_rx_key_state_set(subghz, SubGhzRxKeyStateAddKey);
                } else {
                    furi_string_set(subghz->error_str, "Function requires\nan SD card.");
                    scene_manager_next_scene(subghz->scene_manager, SubGhzSceneShowError);
                }
            }
            consumed = true;
            break;

        case SubGhzCustomEventViewReadRAWSave:
            if(subghz_file_available(subghz) && subghz_scene_read_raw_update_filename(subghz)) {
                scene_manager_set_scene_state(
                    subghz->scene_manager, SubGhzSceneReadRAW, SubGhzCustomEventManagerSetRAW);
                subghz_rx_key_state_set(subghz, SubGhzRxKeyStateBack);
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneSaveName);
            } else {
                if(!scene_manager_search_and_switch_to_previous_scene(
                       subghz->scene_manager, SubGhzSceneStart)) {
                    scene_manager_stop(subghz->scene_manager);
                    view_dispatcher_stop(subghz->view_dispatcher);
                }
            }
            consumed = true;
            break;

        default:
            break;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        switch(subghz->state_notifications) {
        case SubGhzNotificationStateRx:
            notification_message(subghz->notifications, &sequence_blink_cyan_10);
            subghz_read_raw_recording_tick(subghz->subghz_read_raw);

            subghz_read_raw_update_sample_write(
                subghz->subghz_read_raw, subghz_protocol_raw_get_sample_write(decoder_raw));

            SubGhzThresholdRssiData ret_rssi = subghz_threshold_get_rssi_data(
                subghz->threshold_rssi, subghz_txrx_radio_device_get_rssi(subghz->txrx));
            subghz_read_raw_add_data_rssi(
                subghz->subghz_read_raw, ret_rssi.rssi, true );

            break;
        case SubGhzNotificationStateTx:
            notification_message(subghz->notifications, &sequence_blink_magenta_10);
            subghz_read_raw_tick_tx(subghz->subghz_read_raw);

            if(subghz_read_raw_is_playback_overdue(subghz->subghz_read_raw)) {
                view_dispatcher_send_custom_event(
                    subghz->view_dispatcher, SubGhzCustomEventViewReadRAWSendStop);
            }
            break;
        default:

            if(!s_start_countdown_fired && !s_start_cancelled &&
               subghz_read_raw_get_status(subghz->subghz_read_raw) ==
                   SubGhzReadRAWStatusStart) {
                s_start_subtick++;
                if(s_start_subtick >= 10) {
                    s_start_subtick = 0;
                    if(s_start_countdown_sec > 0) {
                        s_start_countdown_sec--;
                        subghz_read_raw_set_start_countdown(
                            subghz->subghz_read_raw, s_start_countdown_sec);
                    }
                    if(s_start_countdown_sec == 0) {
                        s_start_countdown_fired = true;

                        subghz_lowram_mitigate(
                            SUBGHZ_LOWRAM_SOFTLOCK_THRESHOLD +
                            subghz_txrx_receiver_heap_needed(subghz->txrx));

                        subghz_read_raw_set_status(
                            subghz->subghz_read_raw,
                            SubGhzReadRAWStatusREC,
                            "",
                            subghz_threshold_rssi_get(subghz->threshold_rssi));
                        view_dispatcher_send_custom_event(
                            subghz->view_dispatcher, SubGhzCustomEventViewReadRAWREC);
                    }
                }
            }
            break;
        }
    }
    return consumed;
}

void subghz_scene_read_raw_on_exit(void* context) {
    SubGhz* subghz = context;

    subghz_lowram_softlock_release();

    subghz_txrx_stop(subghz->txrx);
    subghz->state_notifications = SubGhzNotificationStateIDLE;
    notification_message(subghz->notifications, &sequence_reset_rgb);

    subghz_txrx_receiver_set_filter(subghz->txrx, subghz->filter);
}
