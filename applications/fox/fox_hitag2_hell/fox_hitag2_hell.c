#include "helpers/hitag2_bf.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <dialogs/dialogs.h>
#include <flipper_format/flipper_format.h>
#include <toolbox/path.h>
#include <loader/loader.h>
#include <stdio.h>
#include <string.h>

#define TAG "FoxHitag2Hell"

#define FIAT_V1_PROTOCOL_NAME "Fiat V1"
#define HITAG2_HELL_MAX_SCAN_FILES 64U
#define HITAG2_HELL_REDRAW_MS 100
#define HITAG2_HELL_SUBGHZ_DIR "/ext/subghz"

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* input_queue;
    FuriMutex* mutex;
    NotificationApp* notifications;

    SubGhzHitag2Bf* bf;
    FuriThread* thread;
    volatile bool user_cancel;
    volatile bool exit_requested;
    uint32_t start_tick;

    FuriString* file_path;

    bool started;
    volatile bool pending_start;
    bool choice_mode;
    uint8_t choice_sel;
    bool done;
    bool success;
    uint8_t level;
    char level_name[16];
    uint8_t progress;
    uint64_t keys_tested;
    uint32_t keys_per_sec;
    uint32_t elapsed_sec;
    uint32_t eta_sec;
    uint8_t captures;
    char message[160];
} Hitag2HellApp;

static bool hitag2_hell_extract_capture(
    FlipperFormat* fff,
    uint32_t* uid_out,
    uint16_t* control_out,
    uint8_t* button_out,
    uint32_t* hop_out) {
    FuriString* proto = furi_string_alloc();
    bool is_fiat_v1 = false;
    flipper_format_rewind(fff);
    if(flipper_format_read_string(fff, "Protocol", proto)) {
        is_fiat_v1 = furi_string_equal_str(proto, FIAT_V1_PROTOCOL_NAME);
    }
    furi_string_free(proto);
    if(!is_fiat_v1) return false;

    uint32_t serial = 0, cnt = 0, btn = 0, hop = 0;
    flipper_format_rewind(fff);
    if(!flipper_format_read_uint32(fff, "Serial", &serial, 1)) return false;
    flipper_format_rewind(fff);
    if(!flipper_format_read_uint32(fff, "Cnt", &cnt, 1)) return false;
    flipper_format_rewind(fff);
    if(!flipper_format_read_uint32(fff, "Btn", &btn, 1)) return false;
    flipper_format_rewind(fff);
    if(!flipper_format_read_uint32(fff, "Hop", &hop, 1)) return false;

    *uid_out = serial;
    *control_out = (uint16_t)(cnt & 0x03FFU);
    *button_out = (uint8_t)(btn & 0x0FU);
    *hop_out = hop;
    return true;
}

static uint8_t hitag2_hell_scan_directory_for_captures(
    SubGhzHitag2Bf* bf,
    const char* main_path,
    uint32_t target_uid) {
    Storage* storage = furi_record_open(RECORD_STORAGE);

    FuriString* dir_path = furi_string_alloc();
    FuriString* main_name = furi_string_alloc();
    path_extract_dirname(main_path, dir_path);
    path_extract_filename_no_ext(main_path, main_name);

    File* dir = storage_file_alloc(storage);
    uint8_t added = 0;

    if(storage_dir_open(dir, furi_string_get_cstr(dir_path))) {
        FileInfo info;
        char name_buf[128];
        uint32_t scanned = 0;
        while(scanned < HITAG2_HELL_MAX_SCAN_FILES &&
              storage_dir_read(dir, &info, name_buf, sizeof(name_buf))) {
            scanned++;
            if(info.flags & FSF_DIRECTORY) continue;
            size_t nlen = strlen(name_buf);
            if(nlen < 5) continue;
            if(strcmp(name_buf + nlen - 4, ".sub") != 0) continue;

            FuriString* candidate_name = furi_string_alloc_set_str(name_buf);
            path_extract_filename(candidate_name, candidate_name, true);
            bool same_as_main = furi_string_equal(candidate_name, main_name);
            furi_string_free(candidate_name);
            if(same_as_main) continue;

            FuriString* full_path = furi_string_alloc_printf(
                "%s/%s", furi_string_get_cstr(dir_path), name_buf);
            FlipperFormat* fff = flipper_format_file_alloc(storage);
            if(flipper_format_file_open_existing(fff, furi_string_get_cstr(full_path))) {
                uint32_t uid;
                uint16_t control;
                uint8_t button;
                uint32_t hop;
                if(hitag2_hell_extract_capture(fff, &uid, &control, &button, &hop)) {
                    if(uid == target_uid) {
                        if(subghz_hitag2_bf_add_capture(bf, uid, control, button, hop)) {
                            added++;
                            if(subghz_hitag2_bf_get_capture_count(bf) >=
                               SUBGHZ_HITAG2_BF_MAX_CAPTURES) {
                                flipper_format_free(fff);
                                furi_string_free(full_path);
                                break;
                            }
                        }
                    }
                }
            }
            flipper_format_free(fff);
            furi_string_free(full_path);
        }
        storage_dir_close(dir);
    }

    storage_file_free(dir);
    furi_string_free(main_name);
    furi_string_free(dir_path);
    furi_record_close(RECORD_STORAGE);

    return added;
}

static void hitag2_hell_write_key_to_file(
    const char* path, const uint8_t key[6], uint32_t epoch) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* fff = flipper_format_file_alloc(storage);

    if(flipper_format_file_open_existing(fff, path)) {
        char key_str[32];
        snprintf(
            key_str,
            sizeof(key_str),
            "%02X %02X %02X %02X %02X %02X",
            key[0],
            key[1],
            key[2],
            key[3],
            key[4],
            key[5]);

        flipper_format_rewind(fff);
        flipper_format_insert_or_update_string_cstr(fff, "Hitag2 Key", key_str);
        flipper_format_rewind(fff);
        flipper_format_insert_or_update_uint32(fff, "Hitag2 Epoch", &epoch, 1);
    } else {
        FURI_LOG_E(TAG, "Failed to reopen .sub for key write-back");
    }

    flipper_format_free(fff);
    furi_record_close(RECORD_STORAGE);
}

static bool hitag2_hell_progress_cb(
    uint8_t level,
    const char* level_name,
    uint8_t progress,
    uint64_t keys_tested,
    void* context) {
    Hitag2HellApp* app = context;
    if(app->user_cancel) return false;

    uint32_t now = furi_get_tick();
    uint32_t elapsed_ms = now - app->start_tick;
    uint32_t elapsed_sec = elapsed_ms / 1000U;
    uint32_t keys_per_sec =
        (elapsed_ms > 0) ? (uint32_t)((keys_tested * 1000ULL) / elapsed_ms) : 0;

    uint32_t eta_sec = 0;
    if(progress > 0 && progress < 100 && keys_per_sec > 0) {
        uint32_t remaining_pct = (uint32_t)(100 - progress);
        eta_sec = (elapsed_sec * remaining_pct) / progress;
        if(eta_sec > 86400U) eta_sec = 86400U;
    }

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->level = level;
    if(level_name) {
        strlcpy(app->level_name, level_name, sizeof(app->level_name));
    }
    app->progress = progress;
    app->keys_tested = keys_tested;
    app->keys_per_sec = keys_per_sec;
    app->elapsed_sec = elapsed_sec;
    app->eta_sec = eta_sec;
    app->captures = subghz_hitag2_bf_get_capture_count(app->bf);
    furi_mutex_release(app->mutex);

    view_port_update(app->view_port);
    return true;
}

static int32_t hitag2_hell_thread(void* context) {
    Hitag2HellApp* app = context;

    bool success = subghz_hitag2_bf_run(app->bf, hitag2_hell_progress_cb, app);

    uint8_t found_key[6] = {0};
    uint32_t found_epoch = 0;
    uint8_t found_level = 0;
    if(success) {
        subghz_hitag2_bf_get_result(app->bf, found_key, &found_epoch, &found_level);
        hitag2_hell_write_key_to_file(
            furi_string_get_cstr(app->file_path), found_key, found_epoch);
    }

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->done = true;
    app->success = success;
    if(success) {
        const char* level_str = "?";
        switch(found_level) {
        case SubGhzHitag2BfLevelKnown: level_str = "Known"; break;
        case SubGhzHitag2BfLevelFlashDict: level_str = "Flash Dict"; break;
        case SubGhzHitag2BfLevelSDDict: level_str = "SD Dict"; break;
        case SubGhzHitag2BfLevelHeuristic: level_str = "Heuristic"; break;
        case SubGhzHitag2BfLevelHitag2Hell: level_str = "Hitag2Hell"; break;
        default: break;
        }
        snprintf(
            app->message,
            sizeof(app->message),
            "%02X %02X %02X %02X %02X %02X\n[%s] epoch=%lu\nSaved to .sub",
            found_key[0],
            found_key[1],
            found_key[2],
            found_key[3],
            found_key[4],
            found_key[5],
            level_str,
            (unsigned long)found_epoch);
    } else if(app->user_cancel) {
        snprintf(app->message, sizeof(app->message), "Cancelled.");
    } else {
        snprintf(
            app->message,
            sizeof(app->message),
            "Tried %lu keys.\n%u capture%s used.\nAdd fobs or SD dict.",
            (unsigned long)subghz_hitag2_bf_get_total_keys_tested(app->bf),
            app->captures,
            app->captures == 1 ? "" : "s");
    }
    furi_mutex_release(app->mutex);

    if(app->notifications) {
        notification_message(app->notifications, success ? &sequence_success : &sequence_error);
    }

    view_port_update(app->view_port);
    return 0;
}

static void hitag2_hell_format_count(char* buf, size_t len, uint64_t count) {
    if(count >= 1000000000ULL) {
        snprintf(
            buf,
            len,
            "%lu.%luG",
            (unsigned long)(count / 1000000000ULL),
            (unsigned long)((count % 1000000000ULL) / 100000000ULL));
    } else if(count >= 1000000ULL) {
        snprintf(
            buf,
            len,
            "%lu.%luM",
            (unsigned long)(count / 1000000ULL),
            (unsigned long)((count % 1000000ULL) / 100000ULL));
    } else if(count >= 1000ULL) {
        snprintf(buf, len, "%luK", (unsigned long)(count / 1000ULL));
    } else {
        snprintf(buf, len, "%lu", (unsigned long)count);
    }
}

static void hitag2_hell_draw_callback(Canvas* canvas, void* context) {
    Hitag2HellApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    canvas_clear(canvas);

    if(!app->started) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 4, AlignCenter, AlignTop, "Hitag2 Hell");
        canvas_set_font(canvas, FontSecondary);
        if(app->choice_mode) {
            elements_multiline_text_aligned(
                canvas, 64, 14, AlignCenter, AlignTop, app->message);

            /* Two Fox-style buttons side by side with a small gap: Add Signal (left, default),
             * Dict Only (right). Left/Right selects; the selected one is drawn filled. */
            const uint8_t bw = 56;
            const uint8_t bh = 13;
            const uint8_t gap = 6;
            const uint8_t by = 49;
            const uint8_t lx = (uint8_t)(64 - gap / 2 - bw);
            const uint8_t rx = (uint8_t)(64 + gap / 2);

            if(app->choice_sel == 0) {
                canvas_draw_rbox(canvas, lx, by, bw, bh, 3);
                canvas_set_color(canvas, ColorWhite);
                canvas_draw_str_aligned(
                    canvas, lx + bw / 2, by + bh / 2, AlignCenter, AlignCenter, "Add Signal");
                canvas_set_color(canvas, ColorBlack);
            } else {
                canvas_draw_rframe(canvas, lx, by, bw, bh, 3);
                canvas_draw_str_aligned(
                    canvas, lx + bw / 2, by + bh / 2, AlignCenter, AlignCenter, "Add Signal");
            }

            if(app->choice_sel == 1) {
                canvas_draw_rbox(canvas, rx, by, bw, bh, 3);
                canvas_set_color(canvas, ColorWhite);
                canvas_draw_str_aligned(
                    canvas, rx + bw / 2, by + bh / 2, AlignCenter, AlignCenter, "Dict Only");
                canvas_set_color(canvas, ColorBlack);
            } else {
                canvas_draw_rframe(canvas, rx, by, bw, bh, 3);
                canvas_draw_str_aligned(
                    canvas, rx + bw / 2, by + bh / 2, AlignCenter, AlignCenter, "Dict Only");
            }
        } else {
            elements_multiline_text_aligned(
                canvas, 64, 20, AlignCenter, AlignTop, app->message);
            elements_button_center(canvas, "Ok");
        }
    } else if(!app->done) {
        canvas_set_font(canvas, FontPrimary);
        char title[40];
        snprintf(
            title,
            sizeof(title),
            "L%u: %s",
            app->level,
            app->level_name[0] ? app->level_name : "Cracking...");
        canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, title);

        canvas_draw_rframe(canvas, 3, 15, 122, 12, 2);
        uint8_t fill = (uint8_t)((uint16_t)app->progress * 116U / 100U);
        if(fill > 2) {
            canvas_draw_rbox(canvas, 5, 17, fill, 8, 1);
        } else if(fill > 0) {
            canvas_draw_box(canvas, 5, 17, fill, 8);
        }

        canvas_set_font(canvas, FontSecondary);

        char keys_str[40];
        char tested_buf[16];
        hitag2_hell_format_count(tested_buf, sizeof(tested_buf), app->keys_tested);
        snprintf(
            keys_str,
            sizeof(keys_str),
            "%u%% - %s keys | %u cap%s",
            app->progress,
            tested_buf,
            app->captures,
            app->captures == 1 ? "" : "s");
        canvas_draw_str(canvas, 2, 38, keys_str);

        char speed_str[40];
        char speed_buf[12];
        hitag2_hell_format_count(speed_buf, sizeof(speed_buf), app->keys_per_sec);
        uint32_t eta_m = app->eta_sec / 60U;
        uint32_t eta_s = app->eta_sec % 60U;
        if(app->eta_sec >= 3600U) {
            snprintf(
                speed_str,
                sizeof(speed_str),
                "%s/s  ETA %luh %lum",
                speed_buf,
                (unsigned long)(app->eta_sec / 3600U),
                (unsigned long)((app->eta_sec % 3600U) / 60U));
        } else if(eta_m > 0) {
            snprintf(
                speed_str,
                sizeof(speed_str),
                "%s/s  ETA %lum %lus",
                speed_buf,
                (unsigned long)eta_m,
                (unsigned long)eta_s);
        } else {
            snprintf(speed_str, sizeof(speed_str), "%s/s  ETA %lus", speed_buf, (unsigned long)eta_s);
        }
        canvas_draw_str(canvas, 2, 48, speed_str);

        char elapsed_str[24];
        uint32_t el_m = app->elapsed_sec / 60U;
        uint32_t el_s = app->elapsed_sec % 60U;
        if(app->elapsed_sec >= 3600U) {
            snprintf(
                elapsed_str,
                sizeof(elapsed_str),
                "Elapsed: %luh %lum",
                (unsigned long)(app->elapsed_sec / 3600U),
                (unsigned long)((app->elapsed_sec % 3600U) / 60U));
        } else if(el_m > 0) {
            snprintf(
                elapsed_str,
                sizeof(elapsed_str),
                "Elapsed: %lum %lus",
                (unsigned long)el_m,
                (unsigned long)el_s);
        } else {
            snprintf(elapsed_str, sizeof(elapsed_str), "Elapsed: %lus", (unsigned long)el_s);
        }
        canvas_draw_str(canvas, 2, 58, elapsed_str);

        canvas_draw_str_aligned(
            canvas,
            126,
            64,
            AlignRight,
            AlignBottom,
            app->user_cancel ? "Cancelling..." : "BACK: Cancel");
    } else {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(
            canvas, 64, 4, AlignCenter, AlignTop, app->success ? "Key Found!" : "Not Cracked");
        canvas_set_font(canvas, FontSecondary);
        elements_multiline_text_aligned(canvas, 64, 20, AlignCenter, AlignTop, app->message);
        elements_button_center(canvas, "Ok");
    }

    furi_mutex_release(app->mutex);
}

static void hitag2_hell_input_callback(InputEvent* event, void* context) {
    Hitag2HellApp* app = context;
    furi_message_queue_put(app->input_queue, event, FuriWaitForever);
}

static Hitag2HellApp* hitag2_hell_app_alloc(void) {
    Hitag2HellApp* app = malloc(sizeof(Hitag2HellApp));
    memset(app, 0, sizeof(*app));

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->file_path = furi_string_alloc();

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, hitag2_hell_draw_callback, app);
    view_port_input_callback_set(app->view_port, hitag2_hell_input_callback, app);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    strlcpy(app->message, "Starting...", sizeof(app->message));

    return app;
}

static void hitag2_hell_app_free(Hitag2HellApp* app) {
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    if(app->bf) subghz_hitag2_bf_free(app->bf);
    furi_kernel_lock();
    furi_string_free(app->file_path);
    furi_message_queue_free(app->input_queue);
    furi_mutex_free(app->mutex);
    free(app);
    furi_kernel_unlock();
}

int32_t fox_hitag2_hell_app(void* p) {
    Hitag2HellApp* app = hitag2_hell_app_alloc();

    bool launched_with_file = (p && ((const char*)p)[0]);

    if(launched_with_file) {
        furi_string_set_str(app->file_path, (const char*)p);
    }

    if(furi_string_size(app->file_path) == 0) {

        DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
        FuriString* picked_path = furi_string_alloc_set(HITAG2_HELL_SUBGHZ_DIR);
        DialogsFileBrowserOptions browser_options;
        dialog_file_browser_set_basic_options(&browser_options, ".sub", NULL);
        browser_options.base_path = HITAG2_HELL_SUBGHZ_DIR;
        if(dialog_file_browser_show(dialogs, picked_path, picked_path, &browser_options)) {
            furi_string_set(app->file_path, picked_path);
        }
        furi_string_free(picked_path);
        furi_record_close(RECORD_DIALOGS);
    }

    bool have_capture = false;
    uint32_t uid = 0;
    uint16_t control = 0;
    uint8_t button = 0;
    uint32_t hop = 0;

    if(furi_string_size(app->file_path) == 0) {
        strlcpy(app->message, "No signal file given.", sizeof(app->message));
    } else {
        Storage* storage = furi_record_open(RECORD_STORAGE);
        FlipperFormat* fff = flipper_format_file_alloc(storage);
        bool is_raw_capture = false;
        if(flipper_format_file_open_existing(fff, furi_string_get_cstr(app->file_path))) {
            have_capture =
                hitag2_hell_extract_capture(fff, &uid, &control, &button, &hop);
            if(!have_capture) {
                FuriString* proto = furi_string_alloc();
                flipper_format_rewind(fff);
                if(flipper_format_read_string(fff, "Protocol", proto)) {
                    is_raw_capture = furi_string_equal_str(proto, "RAW");
                }
                furi_string_free(proto);
            }
        }
        flipper_format_free(fff);
        furi_record_close(RECORD_STORAGE);

        if(!have_capture) {
            if(is_raw_capture) {
                strlcpy(
                    app->message,
                    "RAW capture. Decode &\nsave as Fiat V1 first,\nthen run Hitag2Hell.",
                    sizeof(app->message));
            } else {
                strlcpy(app->message, "Not a Fiat V1 signal.", sizeof(app->message));
            }
        }
    }

    if(have_capture) {
        app->bf = subghz_hitag2_bf_alloc();
        subghz_hitag2_bf_add_capture(app->bf, uid, control, button, hop);
        hitag2_hell_scan_directory_for_captures(
            app->bf, furi_string_get_cstr(app->file_path), uid);

        bool full_recovery = subghz_hitag2_bf_get_capture_count(app->bf) >= 2;
        if(full_recovery) {
            subghz_hitag2_bf_set_levels(app->bf, 0xFFU);
        } else {
            subghz_hitag2_bf_set_levels(
                app->bf,
                (uint8_t)(
                    (1U << SubGhzHitag2BfLevelKnown) |
                    (1U << SubGhzHitag2BfLevelFlashDict) |
                    (1U << SubGhzHitag2BfLevelSDDict)));
        }

        furi_mutex_acquire(app->mutex, FuriWaitForever);
        app->captures = subghz_hitag2_bf_get_capture_count(app->bf);
        app->pending_start = true;
        if(full_recovery) {
            snprintf(
                app->message,
                sizeof(app->message),
                "%u captures found.\nL1-L4 quick. L5 brute\nis VERY slow on-device\n(ESP32 needed). OK/Back",
                app->captures);
        } else {
            app->pending_start = false;
            app->choice_mode = true;
            app->choice_sel = 0;
            strlcpy(
                app->message,
                "1 capture found.\nAdd another fob for\nfull crack, or dict only.",
                sizeof(app->message));
        }
        furi_mutex_release(app->mutex);
    }

    view_port_update(app->view_port);

    InputEvent event;
    bool thread_joined = false;
    bool cancel_requested = false;
    while(!app->exit_requested) {
        if(furi_message_queue_get(app->input_queue, &event, HITAG2_HELL_REDRAW_MS) ==
           FuriStatusOk) {
            if(app->choice_mode && event.type == InputTypeShort) {
                if(event.key == InputKeyLeft) {
                    app->choice_sel = 0;
                    view_port_update(app->view_port);
                } else if(event.key == InputKeyRight) {
                    app->choice_sel = 1;
                    view_port_update(app->view_port);
                } else if(event.key == InputKeyUp || event.key == InputKeyDown) {
                    app->choice_sel = (app->choice_sel == 0) ? 1 : 0;
                    view_port_update(app->view_port);
                } else if(event.key == InputKeyBack) {
                    app->exit_requested = true;
                } else if(event.key == InputKeyOk) {
                    if(app->choice_sel == 1) {
                        app->choice_mode = false;
                        furi_mutex_acquire(app->mutex, FuriWaitForever);
                        app->started = true;
                        furi_mutex_release(app->mutex);
                        app->start_tick = furi_get_tick();
                        app->thread = furi_thread_alloc_ex(
                            "Hitag2Hell", 6144, hitag2_hell_thread, app);
                        furi_thread_start(app->thread);
                        view_port_update(app->view_port);
                    } else {
                        DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
                        FuriString* picked = furi_string_alloc_set(HITAG2_HELL_SUBGHZ_DIR);
                        DialogsFileBrowserOptions bopt;
                        dialog_file_browser_set_basic_options(&bopt, ".sub", NULL);
                        bopt.base_path = HITAG2_HELL_SUBGHZ_DIR;
                        bool picked_ok =
                            dialog_file_browser_show(dialogs, picked, picked, &bopt);
                        furi_record_close(RECORD_DIALOGS);

                        bool added = false;
                        if(picked_ok) {
                            Storage* st = furi_record_open(RECORD_STORAGE);
                            FlipperFormat* af = flipper_format_file_alloc(st);
                            if(flipper_format_file_open_existing(
                                   af, furi_string_get_cstr(picked))) {
                                uint32_t a_uid = 0;
                                uint16_t a_ctrl = 0;
                                uint8_t a_btn = 0;
                                uint32_t a_hop = 0;
                                if(hitag2_hell_extract_capture(
                                       af, &a_uid, &a_ctrl, &a_btn, &a_hop)) {
                                    added = subghz_hitag2_bf_add_capture(
                                        app->bf, a_uid, a_ctrl, a_btn, a_hop);
                                }
                            }
                            flipper_format_free(af);
                            furi_record_close(RECORD_STORAGE);
                        }
                        furi_string_free(picked);

                        furi_mutex_acquire(app->mutex, FuriWaitForever);
                        app->captures = subghz_hitag2_bf_get_capture_count(app->bf);
                        if(app->captures >= 2) {
                            subghz_hitag2_bf_set_levels(app->bf, 0xFFU);
                            app->choice_mode = false;
                            app->pending_start = true;
                            snprintf(
                                app->message,
                                sizeof(app->message),
                                "%u captures ready.\nL5 brute VERY slow on\ndevice (ESP32 needed).\nOK start / Back no.",
                                app->captures);
                        } else {
                            strlcpy(
                                app->message,
                                added ? "Added (same UID).\nNeed 2+ total." :
                                        "Not added: different\nfob or no capture.",
                                sizeof(app->message));
                        }
                        furi_mutex_release(app->mutex);
                        view_port_update(app->view_port);
                    }
                }
            } else if(
                event.type == InputTypeShort &&
                (event.key == InputKeyBack || event.key == InputKeyOk)) {
                if(app->pending_start) {
                    if(event.key == InputKeyOk) {
                        app->pending_start = false;
                        furi_mutex_acquire(app->mutex, FuriWaitForever);
                        app->started = true;
                        furi_mutex_release(app->mutex);
                        app->start_tick = furi_get_tick();
                        app->thread = furi_thread_alloc_ex(
                            "Hitag2Hell", 6144, hitag2_hell_thread, app);
                        furi_thread_start(app->thread);
                        view_port_update(app->view_port);
                    } else {
                        app->exit_requested = true;
                    }
                } else if(app->thread && !thread_joined) {
                    if(!cancel_requested) {
                        cancel_requested = true;
                        app->user_cancel = true;
                        if(app->bf) subghz_hitag2_bf_request_cancel(app->bf);
                        view_port_update(app->view_port);
                    }

                } else {
                    app->exit_requested = true;
                }
            }
        }

        if(cancel_requested && app->thread && !thread_joined) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            bool worker_done = app->done;
            furi_mutex_release(app->mutex);
            if(worker_done) {
                furi_thread_join(app->thread);
                furi_thread_free(app->thread);
                app->thread = NULL;
                thread_joined = true;
                app->exit_requested = true;
            }
        }
    }

    if(app->thread && !thread_joined) {
        app->user_cancel = true;
        furi_thread_join(app->thread);
        furi_thread_free(app->thread);
        app->thread = NULL;
    }

    bool return_to_subghz = launched_with_file && furi_string_size(app->file_path) > 0;
    FuriString* return_path = furi_string_alloc();
    if(return_to_subghz) {
        furi_string_set(return_path, app->file_path);
    }

    hitag2_hell_app_free(app);

    if(return_to_subghz) {
        Storage* storage = furi_record_open(RECORD_STORAGE);
        File* f = storage_file_alloc(storage);
        if(storage_file_open(
               f, "/ext/subghz/.focus_file", FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            const char* km_prefix = "keymethod:";
            storage_file_write(f, km_prefix, strlen(km_prefix));
            storage_file_write(
                f, furi_string_get_cstr(return_path), furi_string_size(return_path));
        }
        storage_file_close(f);
        storage_file_free(f);
        furi_record_close(RECORD_STORAGE);

        Loader* loader = furi_record_open(RECORD_LOADER);
        loader_enqueue_launch(loader, "subghz", NULL, LoaderDeferredLaunchFlagNone);
        furi_record_close(RECORD_LOADER);
    }
    furi_string_free(return_path);

    return 0;
}
