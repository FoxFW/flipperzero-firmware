#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <gui/elements.h>
#include <input/input.h>
#include <storage/storage.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <loader/loader.h>
#include <datetime/datetime.h>
#include <subghz/devices/devices.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <lib/subghz/subghz_worker.h>
#include <lib/subghz/environment.h>
#include <lib/subghz/protocols/raw.h>
#include <lib/subghz/receiver.h>
#include <lib/subghz/protocols/protocol_items.h>
#include <lib/toolbox/stream/stream.h>
#include <lib/toolbox/stream/file_stream.h>
#include <lib/toolbox/strint.h>
#include <fox_subghz_grabber_icons.h>
#include <stdio.h>
#include <string.h>

#define TAG "FoxGrabber"

#define GRAB_TMP_NAME    "fox_grab_tmp"
#define GRAB_TMP_PATH    EXT_PATH("subghz/fox_grab_tmp.sub")
#define GRAB_SAVE_DIR    EXT_PATH("subghz")
#define GRAB_CAPTURE_MS  2000u
#define GRAB_WAIT_MS     6000u
#define GRAB_RSSI_MARGIN 10.0f
#define GRAB_GAP_US      8000
#define GRAB_MIN_EDGES   16
#define GRAB_MAX_EDGES   512
#define GRAB_FRAMES_TARGET 3
#define GRAB_FRAMES_MIN    2

#define GRAB_FINE_STEP_HZ   20000
#define GRAB_FINE_POINTS    31
#define GRAB_FINE_SETTLE_MS 2
#define GRAB_FINE_PRESET    FuriHalSubGhzPresetOok270Async
#define GRAB_AM_PEAK_DB     6.0f

typedef struct {
    const char*         name;
    FuriHalSubGhzPreset preset;
} GrabMod;
static const GrabMod GRAB_MODS[] = {
    {"AM650", FuriHalSubGhzPresetOok650Async},
    {"AM270", FuriHalSubGhzPresetOok270Async},
    {"FM238", FuriHalSubGhzPreset2FSKDev238Async},
    {"FM476", FuriHalSubGhzPreset2FSKDev476Async},
    {"FM12K", FuriHalSubGhzPreset2FSKDev12KAsync},
};
#define GRAB_MOD_COUNT ((int)(sizeof(GRAB_MODS) / sizeof(GRAB_MODS[0])))

typedef struct {
    uint32_t    freq;
    const char* name;
} GrabFreq;
static const GrabFreq GRAB_FREQS[] = {
    {433920000u, "433.92"},
    {315000000u, "315.00"},
    {868350000u, "868.35"},
    {434420000u, "434.42"},
    {303875000u, "303.87"},
    {304250000u, "304.25"},
    {310000000u, "310.00"},
    {318000000u, "318.00"},
    {330000000u, "330.00"},
    {345000000u, "345.00"},
    {390000000u, "390.00"},
    {868950000u, "868.95"},
};
#define GRAB_FREQ_COUNT ((int)(sizeof(GRAB_FREQS) / sizeof(GRAB_FREQS[0])))

#define GRAB_FREQ_MASK_DEFAULT 0x000Fu
#define GRAB_MOD_MASK_DEFAULT  0x000Bu
#define GRAB_CFG_DIR   EXT_PATH("subghz/assets/settings")
#define GRAB_CFG_PATH  GRAB_CFG_DIR "/.fox_grabber.cfg"
#define GRAB_CFG_OLD   EXT_PATH("subghz/.fox_grabber.cfg")
#define GRAB_CFG_MAGIC 0x47524231u

typedef enum {
    GrabScreenStart,
    GrabScreenConfig,
    GrabScreenProgress,
    GrabScreenResult,
} GrabScreen;

typedef enum {
    GrabStageIdle,
    GrabStageFreq,
    GrabStageMod,
    GrabStageRead,
    GrabStageAnalyze,
    GrabStageDone,
    GrabStageError,
} GrabStage;

typedef struct {
    Gui*              gui;
    ViewPort*         view_port;
    FuriMessageQueue* input_queue;
    FuriMutex*        mutex;
    NotificationApp*  notifications;
    FuriThread*       worker;

    GrabScreen screen;
    GrabStage  stage;
    bool       header_pleasewait;
    int        countdown;
    bool       exit_requested;
    bool       open_in_subghz;

    uint32_t   locked_freq;
    int        locked_preset;
    uint32_t   freq_mask;
    uint32_t   mod_mask;
    int        cfg_cursor;
    int        cfg_top;
    int        result_rc;
    char       saved_name[64];
    char       saved_freq[12];
    char       saved_mod[12];
    char       saved_proto[24];
    int        result_sel;
    int        start_sel;
    int        err_sel;
    bool       dev_inited;
    uint32_t   name_scroll;
} GrabApp;

#define GRAB_PROTO_SZ 24

typedef struct {
    void*           raw;
    SubGhzReceiver* receiver;
} GrabFeed;

static void grab_feed_cb(void* ctx, bool level, uint32_t duration) {
    GrabFeed* fd = ctx;
    subghz_protocol_decoder_raw_feed(fd->raw, level, duration);
    subghz_receiver_decode(fd->receiver, level, duration);
}

static void grab_overrun_cb(void* ctx) {
    GrabFeed* fd = ctx;
    subghz_protocol_decoder_raw_reset(fd->raw);
    subghz_receiver_reset(fd->receiver);
}

static void grab_rx_cb(
    SubGhzReceiver* receiver, SubGhzProtocolDecoderBase* decoder_base, void* ctx) {
    char* out = ctx;
    if(out && out[0] == '\0' && decoder_base && decoder_base->protocol &&
       decoder_base->protocol->name) {
        strncpy(out, decoder_base->protocol->name, GRAB_PROTO_SZ - 1);
        out[GRAB_PROTO_SZ - 1] = '\0';
    }
    subghz_receiver_reset(receiver);
}

static bool grab_capture(
    const SubGhzDevice* device,
    uint32_t freq,
    FuriHalSubGhzPreset preset,
    const char* preset_name,
    uint32_t ms,
    char* proto_out) {
    SubGhzEnvironment* env = subghz_environment_alloc();
    void* raw = subghz_protocol_decoder_raw_alloc(env);

    subghz_environment_set_protocol_registry(env, &subghz_protocol_registry);
    SubGhzReceiver* receiver = subghz_receiver_alloc_init(env);
    subghz_receiver_set_filter(receiver, SubGhzProtocolFlag_Decodable);
    if(proto_out) proto_out[0] = '\0';
    subghz_receiver_set_rx_callback(receiver, grab_rx_cb, proto_out);
    GrabFeed feed = {.raw = raw, .receiver = receiver};

    SubGhzWorker* worker = subghz_worker_alloc();
    subghz_worker_set_overrun_callback(
        worker, (SubGhzWorkerOverrunCallback)grab_overrun_cb);
    subghz_worker_set_pair_callback(
        worker, (SubGhzWorkerPairCallback)grab_feed_cb);
    subghz_worker_set_context(worker, &feed);

    SubGhzRadioPreset rp;
    rp.name = furi_string_alloc_set(preset_name);
    rp.frequency = freq;
    rp.data = NULL;
    rp.data_size = 0;

    subghz_devices_reset(device);
    subghz_devices_idle(device);
    subghz_devices_load_preset(device, preset, NULL);
    subghz_devices_set_frequency(device, freq);

    bool ok = subghz_protocol_raw_save_to_file_init(
        (SubGhzProtocolDecoderRAW*)raw, GRAB_TMP_NAME, &rp);
    if(ok) {
        subghz_devices_start_async_rx(device, subghz_worker_rx_callback, worker);
        subghz_worker_start(worker);
        uint32_t start = furi_get_tick();
        while((furi_get_tick() - start) < furi_ms_to_ticks(ms)) {
            furi_delay_ms(20);
        }
        subghz_worker_stop(worker);
        subghz_devices_stop_async_rx(device);
        subghz_protocol_raw_save_to_file_stop((SubGhzProtocolDecoderRAW*)raw);
    }

    furi_string_free(rp.name);
    subghz_worker_free(worker);
    subghz_receiver_free(receiver);
    subghz_protocol_decoder_raw_free(raw);
    subghz_environment_free(env);
    return ok;
}

static int grab_segment_and_save(const char* out_path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* in = file_stream_alloc(storage);
    Stream* out = file_stream_alloc(storage);
    FuriString* line = furi_string_alloc();
    int32_t* frame = malloc(sizeof(int32_t) * GRAB_MAX_EDGES);
    int fcount = 0;
    int frames = 0;
    int last_guard_end = 0;
    int state = 0;
    int rc = -1;

    do {
        if(!frame) break;
        if(!file_stream_open(in, GRAB_TMP_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) break;
        if(!file_stream_open(out, out_path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) break;

        while(stream_read_line(in, line)) {
            char* rd = strstr(furi_string_get_cstr(line), "RAW_Data:");
            if(!rd) {

                furi_string_trim(line);
                stream_write_string(out, line);
                stream_write_cstring(out, "\n");
                continue;
            }
            char* p = strchr(rd, ' ');
            int32_t d;
            while(p && strint_to_int32(p, &p, &d, 10) == StrintParseNoError) {
                bool is_gap = (d < 0) && (-d >= GRAB_GAP_US);
                if(state == 0) {
                    if(is_gap) {

                        frame[fcount++] = d;
                        last_guard_end = fcount;
                        state = 1;
                    }
                } else if(state == 1) {
                    if(fcount < GRAB_MAX_EDGES) {
                        frame[fcount++] = d;
                    } else {
                        state = 3;
                        break;
                    }
                    if(is_gap) {

                        frames++;
                        last_guard_end = fcount;
                        if(frames >= GRAB_FRAMES_TARGET) {
                            state = 2;
                            break;
                        }
                    }
                }
                if(*p == ',') p++;
            }
            if(state >= 2) break;
        }

        if(last_guard_end > 0) fcount = last_guard_end;
        if(frames < GRAB_FRAMES_MIN || fcount < GRAB_MIN_EDGES) {
            rc = -2;
            break;
        }

        FuriString* row = furi_string_alloc_set("RAW_Data:");
        for(int i = 0; i < fcount; i++) {
            furi_string_cat_printf(row, " %ld", (long)frame[i]);
        }
        stream_write_string(out, row);
        stream_write_cstring(out, "\n");
        furi_string_free(row);
        rc = 0;
    } while(0);

    if(frame) free(frame);
    furi_string_free(line);
    file_stream_close(in);
    stream_free(in);
    file_stream_close(out);
    stream_free(out);
    if(rc != 0) storage_simply_remove(storage, out_path);
    furi_record_close(RECORD_STORAGE);
    return rc;
}

static void grab_set(GrabApp* app, GrabScreen screen, GrabStage stage) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->screen = screen;
    app->stage = stage;
    if(screen == GrabScreenStart) app->start_sel = 1;
    if(screen == GrabScreenResult) app->err_sel = 1;
    if(screen == GrabScreenResult) app->result_sel = 1;
    furi_mutex_release(app->mutex);
}

static void grab_cfg_load(GrabApp* app) {
    app->freq_mask = GRAB_FREQ_MASK_DEFAULT;
    app->mod_mask  = GRAB_MOD_MASK_DEFAULT;
    Storage* st = furi_record_open(RECORD_STORAGE);
    File* f = storage_file_alloc(st);
    if(storage_file_open(f, GRAB_CFG_PATH, FSAM_READ, FSOM_OPEN_EXISTING) ||
       storage_file_open(f, GRAB_CFG_OLD, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint32_t magic = 0; uint16_t fm = 0; uint8_t mm = 0;
        if(storage_file_read(f, &magic, sizeof(magic)) == sizeof(magic) && magic == GRAB_CFG_MAGIC &&
           storage_file_read(f, &fm, sizeof(fm)) == sizeof(fm) &&
           storage_file_read(f, &mm, sizeof(mm)) == sizeof(mm)) {
            app->freq_mask = fm;
            app->mod_mask  = mm;
        }
        storage_file_close(f);
    }
    storage_file_free(f);
    furi_record_close(RECORD_STORAGE);
}

static void grab_cfg_save(GrabApp* app) {
    Storage* st = furi_record_open(RECORD_STORAGE);
    File* f = storage_file_alloc(st);
    storage_simply_mkdir(st, GRAB_CFG_DIR);
    storage_simply_remove(st, GRAB_CFG_OLD);
    if(storage_file_open(f, GRAB_CFG_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        uint32_t magic = GRAB_CFG_MAGIC;
        uint16_t fm = (uint16_t)app->freq_mask;
        uint8_t  mm = (uint8_t)app->mod_mask;
        storage_file_write(f, &magic, sizeof(magic));
        storage_file_write(f, &fm, sizeof(fm));
        storage_file_write(f, &mm, sizeof(mm));
        storage_file_close(f);
    }
    storage_file_free(f);
    furi_record_close(RECORD_STORAGE);
}

static bool grab_mod_is_am(FuriHalSubGhzPreset p) {
    return p == FuriHalSubGhzPresetOok650Async || p == FuriHalSubGhzPresetOok270Async;
}

static int32_t grab_worker(void* ctx) {
    GrabApp* app = ctx;

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    uint32_t fmask = app->freq_mask;
    uint32_t mmask = app->mod_mask;
    furi_mutex_release(app->mutex);

    uint32_t    freqs[GRAB_FREQ_COUNT];
    const char* fnames[GRAB_FREQ_COUNT];
    int nfreq = 0;
    for(int i = 0; i < GRAB_FREQ_COUNT; i++) {
        if((fmask >> i) & 1u) {
            freqs[nfreq]  = GRAB_FREQS[i].freq;
            fnames[nfreq] = GRAB_FREQS[i].name;
            nfreq++;
        }
    }
    int mods[GRAB_MOD_COUNT];
    int nmod = 0;
    for(int i = 0; i < GRAB_MOD_COUNT; i++) {
        if((mmask >> i) & 1u) mods[nmod++] = i;
    }
    if(nfreq == 0 || nmod == 0) {
        app->result_rc = -4;
        grab_set(app, GrabScreenResult, GrabStageError);
        return 0;
    }

    const SubGhzDevice* device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    if(!device) {
        app->result_rc = -1;
        grab_set(app, GrabScreenResult, GrabStageError);
        return 0;
    }
    subghz_devices_begin(device);

    const FuriHalSubGhzPreset ref_preset = GRAB_MODS[mods[0]].preset;

    float noise[GRAB_FREQ_COUNT];
    subghz_devices_reset(device);
    subghz_devices_idle(device);
    subghz_devices_load_preset(device, ref_preset, NULL);
    for(int i = 0; i < nfreq; i++) {
        subghz_devices_idle(device);
        subghz_devices_set_frequency(device, freqs[i]);
        subghz_devices_set_rx(device);
        float nf = subghz_devices_get_rssi(device);
        for(int k = 0; k < 3; k++) {
            float r = subghz_devices_get_rssi(device);
            if(r > nf) nf = r;
            furi_delay_ms(5);
        }
        noise[i] = nf;
    }
    subghz_devices_idle(device);

    for(int n = 3; n >= 1; n--) {
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        app->screen = GrabScreenProgress;
        app->stage = GrabStageFreq;
        app->header_pleasewait = false;
        app->countdown = n;
        furi_mutex_release(app->mutex);
        notification_message(app->notifications, &sequence_single_vibro);
        furi_delay_ms(1000);
    }
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->countdown = 0;
    app->stage = GrabStageFreq;
    furi_mutex_release(app->mutex);

    notification_message(app->notifications, &sequence_set_vibro_on);
    notification_message(app->notifications, &sequence_blink_start_cyan);

    int fi = -1;
    uint32_t wait_start = furi_get_tick();
    while(fi < 0 && (furi_get_tick() - wait_start) < furi_ms_to_ticks(GRAB_WAIT_MS)) {
        float best_margin = GRAB_RSSI_MARGIN;
        int best_i = -1;
        for(int i = 0; i < nfreq; i++) {
            subghz_devices_idle(device);
            subghz_devices_set_frequency(device, freqs[i]);
            subghz_devices_set_rx(device);
            furi_delay_ms(3);
            float margin = subghz_devices_get_rssi(device) - noise[i];
            if(margin > best_margin) {
                best_margin = margin;
                best_i = i;
            }
        }
        fi = best_i;
    }
    subghz_devices_idle(device);

    int rc = -3;
    char path[96];
    if(fi >= 0) {

        uint32_t cap_freq = freqs[fi];
        bool is_am = false;
        bool have_spectral = false;
        {
            float fine_r[GRAB_FINE_POINTS];
            int32_t base = (int32_t)freqs[fi] - (GRAB_FINE_POINTS / 2) * GRAB_FINE_STEP_HZ;
            int peak_s = GRAB_FINE_POINTS / 2;
            float peak_r = -200.0f;
            subghz_devices_idle(device);
            subghz_devices_load_preset(device, GRAB_FINE_PRESET, NULL);
            for(int s = 0; s < GRAB_FINE_POINTS; s++) {
                fine_r[s] = -200.0f;
                uint32_t f = (uint32_t)(base + s * GRAB_FINE_STEP_HZ);
                if(!subghz_devices_is_frequency_valid(device, f)) continue;
                subghz_devices_idle(device);
                subghz_devices_set_frequency(device, f);
                subghz_devices_set_rx(device);
                furi_delay_ms(GRAB_FINE_SETTLE_MS);
                float r = subghz_devices_get_rssi(device);
                fine_r[s] = r;
                if(r > peak_r) {
                    peak_r = r;
                    peak_s = s;
                }
            }
            uint32_t pf = (uint32_t)(base + peak_s * GRAB_FINE_STEP_HZ);
            if(subghz_devices_is_frequency_valid(device, pf)) cap_freq = pf;

            if(peak_s > 0 && peak_s < GRAB_FINE_POINTS - 1 && fine_r[peak_s - 1] > -199.0f &&
               fine_r[peak_s + 1] > -199.0f) {
                float dl = fine_r[peak_s] - fine_r[peak_s - 1];
                float dr = fine_r[peak_s] - fine_r[peak_s + 1];
                is_am = (dl >= GRAB_AM_PEAK_DB && dr >= GRAB_AM_PEAK_DB);
                have_spectral = true;
            }
            subghz_devices_idle(device);
        }

        int order[GRAB_MOD_COUNT];
        int no = 0;
        if(have_spectral) {
            for(int k = 0; k < nmod; k++)
                if(grab_mod_is_am(GRAB_MODS[mods[k]].preset) == is_am) order[no++] = mods[k];
            for(int k = 0; k < nmod; k++)
                if(grab_mod_is_am(GRAB_MODS[mods[k]].preset) != is_am) order[no++] = mods[k];
        } else {
            for(int k = 0; k < nmod; k++) order[no++] = mods[k];
        }

        DateTime dt;
        furi_hal_rtc_get_datetime(&dt);
        rc = -2;
        for(int k = 0; k < no; k++) {
            int m = order[k];
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            app->stage = GrabStageMod;
            app->header_pleasewait = false;
            furi_mutex_release(app->mutex);

            char proto[GRAB_PROTO_SZ] = {0};
            bool cap = grab_capture(
                device, cap_freq, GRAB_MODS[m].preset, GRAB_MODS[m].name, GRAB_CAPTURE_MS,
                proto);
            if(!cap) continue;

            furi_mutex_acquire(app->mutex, FuriWaitForever);
            app->stage = GrabStageAnalyze;
            app->header_pleasewait = true;
            furi_mutex_release(app->mutex);

            snprintf(
                path, sizeof(path), "%s/Grab_%02u%02u_%02u%02u%02u_%s_%s.sub",
                GRAB_SAVE_DIR, dt.month, dt.day, dt.hour, dt.minute, dt.second,
                fnames[fi], GRAB_MODS[m].name);
            int one = grab_segment_and_save(path);

            Storage* st = furi_record_open(RECORD_STORAGE);
            storage_simply_remove(st, GRAB_TMP_PATH);
            furi_record_close(RECORD_STORAGE);

            if(one == 0) {
                rc = 0;
                strncpy(app->saved_freq, fnames[fi], sizeof(app->saved_freq) - 1);
                strncpy(app->saved_mod, GRAB_MODS[m].name, sizeof(app->saved_mod) - 1);

                if(proto[0]) {
                    strncpy(app->saved_proto, proto, sizeof(app->saved_proto) - 1);
                } else {
                    app->saved_proto[0] = '\0';
                }
                app->saved_proto[sizeof(app->saved_proto) - 1] = '\0';
                break;
            }
        }
    }

    notification_message(app->notifications, &sequence_reset_vibro);
    notification_message(app->notifications, &sequence_blink_stop);
    subghz_devices_idle(device);
    subghz_devices_sleep(device);
    subghz_devices_end(device);

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->result_rc = rc;
    if(rc == 0) {
        strncpy(app->saved_name, path, sizeof(app->saved_name) - 1);
        app->saved_name[sizeof(app->saved_name) - 1] = '\0';
        app->result_sel = 1;
        app->stage = GrabStageDone;
    } else {
        app->stage = GrabStageError;
    }
    app->screen = GrabScreenResult;
    furi_mutex_release(app->mutex);
    return 0;
}

static const char* grab_stage_text(GrabStage s) {
    switch(s) {
    case GrabStageFreq:    return "Detecting Freq.";
    case GrabStageMod:     return "Detecting Modulation";
    case GrabStageRead:    return "Reading Signal";
    case GrabStageAnalyze: return "Analyzing Signal";
    default:               return "";
    }
}

#define GRAB_BTN_H 13

static void grab_two_btn(Canvas* c, bool focus_left, const char* left, const char* right) {
    int y = 50, gap = 4;
    int w = (128 - gap * 3) / 2;
    int lx = gap, rx = gap * 2 + w;
    canvas_set_font(c, FontSecondary);
    canvas_set_color(c, ColorBlack);
    if(focus_left)
        canvas_draw_rbox(c, lx, y, w, GRAB_BTN_H, 3);
    else
        canvas_draw_rframe(c, lx, y, w, GRAB_BTN_H, 3);
    if(focus_left) canvas_set_color(c, ColorWhite);
    canvas_draw_str_aligned(c, lx + w / 2, y + GRAB_BTN_H / 2, AlignCenter, AlignCenter, left);
    canvas_set_color(c, ColorBlack);
    if(!focus_left)
        canvas_draw_rbox(c, rx, y, w, GRAB_BTN_H, 3);
    else
        canvas_draw_rframe(c, rx, y, w, GRAB_BTN_H, 3);
    if(!focus_left) canvas_set_color(c, ColorWhite);
    canvas_draw_str_aligned(c, rx + w / 2, y + GRAB_BTN_H / 2, AlignCenter, AlignCenter, right);
    canvas_set_color(c, ColorBlack);
}

static void grab_three_btn(Canvas* c, int focus, const char* a, const char* b, const char* cc) {
    int y = 50, gap = 4;
    int w = (128 - gap * 4) / 3;
    int x[3] = {gap, gap * 2 + w, gap * 3 + w * 2};
    const char* lbl[3] = {a, b, cc};
    canvas_set_font(c, FontSecondary);
    for(int i = 0; i < 3; i++) {
        canvas_set_color(c, ColorBlack);
        if(i == focus)
            canvas_draw_rbox(c, x[i], y, w, GRAB_BTN_H, 3);
        else
            canvas_draw_rframe(c, x[i], y, w, GRAB_BTN_H, 3);
        if(i == focus) canvas_set_color(c, ColorWhite);
        canvas_draw_str_aligned(
            c, x[i] + w / 2, y + GRAB_BTN_H / 2, AlignCenter, AlignCenter, lbl[i]);
    }
    canvas_set_color(c, ColorBlack);
}
static void draw_start(Canvas* c, GrabApp* app) {
    canvas_draw_icon(c, 2, 14, &I_fox_32x32);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 42, 26, "Press & Hold");
    canvas_draw_str(c, 42, 40, "KeyFob!");
    grab_two_btn(c, app->start_sel == 0, "Config", "Start");
}

static void draw_config(Canvas* c, GrabApp* app) {
    const int total = GRAB_FREQ_COUNT + GRAB_MOD_COUNT;
    canvas_set_font(c, FontPrimary);
    const char* title = (app->cfg_cursor < GRAB_FREQ_COUNT) ? "Frequencies" : "Modulations";
    canvas_draw_str(c, 2, 9, title);
    canvas_draw_line(c, 0, 11, 127, 11);
    canvas_set_font(c, FontSecondary);
    for(int r = 0; r < 5; r++) {
        int e = app->cfg_top + r;
        if(e >= total) break;
        int y = 21 + r * 10;
        bool is_freq = (e < GRAB_FREQ_COUNT);
        bool en;
        const char* label;
        if(is_freq) {
            en = (app->freq_mask >> e) & 1u;
            label = GRAB_FREQS[e].name;
        } else {
            int m = e - GRAB_FREQ_COUNT;
            en = (app->mod_mask >> m) & 1u;
            label = GRAB_MODS[m].name;
        }
        bool sel = (e == app->cfg_cursor);
        if(sel) {
            canvas_draw_box(c, 0, y - 9, 128, 11);
            canvas_set_color(c, ColorWhite);
        }
        canvas_draw_frame(c, 3, y - 8, 7, 7);
        if(en) canvas_draw_box(c, 5, y - 6, 3, 3);
        canvas_draw_str(c, 15, y, label);
        if(is_freq) canvas_draw_str(c, 58, y, "MHz");
        if(sel) canvas_set_color(c, ColorBlack);
    }
}

static void draw_progress(Canvas* c, GrabApp* app) {
    canvas_set_font(c, FontPrimary);
    if(app->countdown > 0) {
        char buf[24];
        snprintf(buf, sizeof(buf), "Press in %d", app->countdown);
        canvas_draw_str_aligned(c, 64, 26, AlignCenter, AlignCenter, buf);
        canvas_set_font(c, FontSecondary);
        canvas_draw_str_aligned(c, 64, 46, AlignCenter, AlignCenter, "Get your fob ready");
        return;
    }
    const char* hdr = app->header_pleasewait ? "Please Wait!" : "Press & Hold KeyFob!";
    canvas_draw_str_aligned(c, 64, 6, AlignCenter, AlignTop, hdr);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str_aligned(c, 64, 32, AlignCenter, AlignCenter, grab_stage_text(app->stage));

    uint8_t w = (app->stage == GrabStageAnalyze) ? 108 : 54;
    canvas_draw_frame(c, 10, 46, 108, 8);
    canvas_draw_box(c, 10, 46, w, 8);
}

static void draw_result(Canvas* c, GrabApp* app) {
    if(app->result_rc == 0) {
        const char* base = strrchr(app->saved_name, '/');
        base = base ? base + 1 : app->saved_name;
        canvas_set_font(c, FontPrimary);
        canvas_draw_str_aligned(c, 64, 6, AlignCenter, AlignTop, "Saved!");
        canvas_set_font(c, FontSecondary);
        elements_scrollable_text_line_str(
            c, 64, 22, 120, base, app->name_scroll / 3, false, true);
        char fl[40];
        snprintf(fl, sizeof(fl), "Freq: %s %s", app->saved_freq, app->saved_mod);
        canvas_draw_str_aligned(c, 64, 31, AlignCenter, AlignCenter, fl);
        if(app->saved_proto[0]) {
            char pl[40];
            snprintf(pl, sizeof(pl), "ID: %s", app->saved_proto);
            canvas_draw_str_aligned(c, 64, 42, AlignCenter, AlignCenter, pl);
        }
        int rs = (app->result_sel < 0 || app->result_sel > 2) ? 1 : app->result_sel;
        grab_three_btn(c, rs, "New", "Open", "Exit");
    } else {
        const char* msg = (app->result_rc == -2) ? "No clean frame" :
                          (app->result_rc == -3) ? "No signal seen" :
                          (app->result_rc == -4) ? "Nothing enabled" : "Capture failed";
        const char* hint =
            (app->result_rc == -4) ? "Pick freqs + mods" :
            (app->result_rc == -3) ? "Hold fob / check freqs" : "Hold the fob longer";
        canvas_set_font(c, FontPrimary);
        canvas_draw_str_aligned(c, 64, 8, AlignCenter, AlignTop, msg);
        canvas_set_font(c, FontSecondary);
        canvas_draw_str_aligned(c, 64, 30, AlignCenter, AlignCenter, hint);
        grab_two_btn(c, app->err_sel == 0, "Exit", "Retry");
    }
}

static void draw_cb(Canvas* c, void* ctx) {
    GrabApp* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    GrabScreen screen = app->screen;
    canvas_clear(c);
    switch(screen) {
    case GrabScreenProgress: draw_progress(c, app); break;
    case GrabScreenResult:   draw_result(c, app); break;
    case GrabScreenConfig:   draw_config(c, app); break;
    case GrabScreenStart:
    default:                 draw_start(c, app); break;
    }
    furi_mutex_release(app->mutex);
}

static void input_cb(InputEvent* event, void* ctx) {
    FuriMessageQueue* queue = ctx;
    furi_message_queue_put(queue, event, FuriWaitForever);
}

static void grab_start_worker(GrabApp* app) {
    if(app->worker) return;
    app->header_pleasewait = false;
    app->result_rc = 0;
    app->countdown = 3;
    app->screen = GrabScreenProgress;
    app->stage = GrabStageRead;
    if(!app->dev_inited) {
        subghz_devices_init();
        app->dev_inited = true;
    }
    app->worker = furi_thread_alloc_ex("FoxGrabWork", 2048, grab_worker, app);
    furi_thread_start(app->worker);
}

static void grab_reap_worker(GrabApp* app) {
    if(app->worker && furi_thread_get_state(app->worker) == FuriThreadStateStopped) {
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
        app->worker = NULL;
    }
}

static GrabApp* grab_alloc(void) {
    GrabApp* app = malloc(sizeof(GrabApp));
    memset(app, 0, sizeof(*app));
    app->mutex         = furi_mutex_alloc(FuriMutexTypeNormal);
    app->screen        = GrabScreenStart;
    app->stage         = GrabStageIdle;
    app->locked_preset = -1;
    app->start_sel     = 1;
    grab_cfg_load(app);

    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->gui           = furi_record_open(RECORD_GUI);
    app->view_port     = view_port_alloc();
    app->input_queue   = furi_message_queue_alloc(8, sizeof(InputEvent));
    view_port_draw_callback_set(app->view_port, draw_cb, app);
    view_port_input_callback_set(app->view_port, input_cb, app->input_queue);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
    return app;
}

static void grab_free(GrabApp* app) {
    if(app->worker) {
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
    }
    notification_message(app->notifications, &sequence_reset_vibro);
    notification_message(app->notifications, &sequence_blink_stop);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_message_queue_free(app->input_queue);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_mutex_free(app->mutex);
    free(app);
}

int32_t fox_subghz_grabber_app(void* p) {
    UNUSED(p);
    GrabApp* app = grab_alloc();

    InputEvent event;
    while(!app->exit_requested) {
        grab_reap_worker(app);
        if(furi_message_queue_get(app->input_queue, &event, 100) == FuriStatusOk) {
            if(event.type == InputTypeShort) {
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                GrabScreen screen = app->screen;
                int rc = app->result_rc;
                furi_mutex_release(app->mutex);
                if(screen == GrabScreenConfig) {

                    const int total = GRAB_FREQ_COUNT + GRAB_MOD_COUNT;
                    furi_mutex_acquire(app->mutex, FuriWaitForever);
                    if(event.key == InputKeyUp) {
                        if(app->cfg_cursor > 0) app->cfg_cursor--;
                        if(app->cfg_cursor < app->cfg_top) app->cfg_top = app->cfg_cursor;
                    } else if(event.key == InputKeyDown) {
                        if(app->cfg_cursor < total - 1) app->cfg_cursor++;
                        if(app->cfg_cursor > app->cfg_top + 4) app->cfg_top = app->cfg_cursor - 4;
                    } else if(event.key == InputKeyOk) {
                        int e = app->cfg_cursor;
                        if(e < GRAB_FREQ_COUNT) app->freq_mask ^= (1u << e);
                        else app->mod_mask ^= (1u << (e - GRAB_FREQ_COUNT));
                    }
                    bool back = (event.key == InputKeyBack);
                    furi_mutex_release(app->mutex);
                    if(back) {
                        grab_cfg_save(app);
                        grab_set(app, GrabScreenStart, GrabStageIdle);
                    }
                } else if(event.key == InputKeyBack) {
                    if(screen != GrabScreenProgress) app->exit_requested = true;
                } else if(event.key == InputKeyOk) {
                    if(screen == GrabScreenStart) {
                        furi_mutex_acquire(app->mutex, FuriWaitForever);
                        int s = app->start_sel;
                        if(s == 0) {
                            app->cfg_cursor = 0;
                            app->cfg_top = 0;
                            app->screen = GrabScreenConfig;
                        }
                        furi_mutex_release(app->mutex);
                        if(s != 0) grab_start_worker(app);
                    } else if(screen == GrabScreenResult && rc != 0) {
                        furi_mutex_acquire(app->mutex, FuriWaitForever);
                        int s = app->err_sel;
                        furi_mutex_release(app->mutex);
                        if(s == 0)
                            app->exit_requested = true;
                        else
                            grab_set(app, GrabScreenStart, GrabStageIdle);
                    } else if(screen == GrabScreenResult) {
                        furi_mutex_acquire(app->mutex, FuriWaitForever);
                        int sel = app->result_sel;
                        furi_mutex_release(app->mutex);
                        if(sel == 0) {
                            grab_set(app, GrabScreenStart, GrabStageIdle);
                        } else if(sel == 1) {
                            app->open_in_subghz = true;
                            app->exit_requested = true;
                        } else {
                            app->exit_requested = true;
                        }
                    }
                } else if(
                    (screen == GrabScreenStart || screen == GrabScreenResult) &&
                    (event.key == InputKeyLeft || event.key == InputKeyRight)) {
                    furi_mutex_acquire(app->mutex, FuriWaitForever);
                    if(screen == GrabScreenStart) {
                        app->start_sel = (event.key == InputKeyLeft) ? 0 : 1;
                    } else if(rc != 0) {
                        app->err_sel = (event.key == InputKeyLeft) ? 0 : 1;
                    } else {
                        if(event.key == InputKeyLeft && app->result_sel > 0) app->result_sel--;
                        if(event.key == InputKeyRight && app->result_sel < 2) app->result_sel++;
                    }
                    furi_mutex_release(app->mutex);
                }
            }
        }
        app->name_scroll++;
        view_port_update(app->view_port);
    }

    bool open = app->open_in_subghz;
    char open_path[64];
    strncpy(open_path, app->saved_name, sizeof(open_path) - 1);
    open_path[sizeof(open_path) - 1] = '\0';

    bool dev_inited = app->dev_inited;
    grab_free(app);
    if(dev_inited) subghz_devices_deinit();

    Loader* loader = furi_record_open(RECORD_LOADER);
    if(open) {
        Storage* grab_storage = furi_record_open(RECORD_STORAGE);
        storage_simply_remove(grab_storage, "/ext/subghz/.focus_menu");
        File* focus_f = storage_file_alloc(grab_storage);
        if(storage_file_open(
               focus_f, "/ext/subghz/.focus_file", FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            const char* rr_prefix = "rawreturn:";
            storage_file_write(focus_f, rr_prefix, strlen(rr_prefix));
            storage_file_write(focus_f, open_path, strlen(open_path));
        }
        storage_file_close(focus_f);
        storage_file_free(focus_f);
        furi_record_close(RECORD_STORAGE);
        loader_enqueue_launch(loader, "Sub-GHz", NULL, LoaderDeferredLaunchFlagGui);
    } else {

        loader_enqueue_launch(loader, "Sub-GHz", NULL, LoaderDeferredLaunchFlagGui);
    }
    furi_record_close(RECORD_LOADER);
    return 0;
}
