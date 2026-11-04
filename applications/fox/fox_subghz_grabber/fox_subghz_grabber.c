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
#define GRAB_CAPTURE_MS  2000u   /* recording window AFTER the signal is seen */
#define GRAB_WAIT_MS     6000u   /* listen this long for the fob after the countdown */
#define GRAB_RSSI_MARGIN 10.0f   /* dB above the sampled noise floor = signal present */
#define GRAB_GAP_US      8000    /* inter-frame gap threshold (us) */
#define GRAB_MIN_EDGES   16      /* a valid frame needs at least this many edges */
#define GRAB_MAX_EDGES   512     /* cap for the saved repeats (3 frames ~= 160; 2 KB buf) */
#define GRAB_FRAMES_TARGET 3     /* save this many repeats (decoders need >=2 identical) */
#define GRAB_FRAMES_MIN    2     /* ...accept this many if the window ran short */

/* ---- fine frequency refine + AM/FM spectral pre-pick ---------------------
 * Technique lifted from the PP Remote Analyzer worker. After the coarse band is
 * locked, sweep +/-300 kHz in 20 kHz steps at a narrow OOK BW to nail the exact
 * center, and use the peak-vs-neighbour spectral shape to guess AM(OOK = sharp
 * spike) vs FM(2FSK = broad) so the capture tries the right modulation family
 * first instead of brute-forcing all. A wrong guess only costs ordering: the
 * other family is still tried afterwards. Thresholds are tune-on-hardware. */
#define GRAB_FINE_STEP_HZ   20000      /* 20 kHz steps */
#define GRAB_FINE_POINTS    31         /* +/-300 kHz / 20 kHz, inclusive = 31 points */
#define GRAB_FINE_SETTLE_MS 2          /* per-point RX settle before the RSSI read */
#define GRAB_FINE_PRESET    FuriHalSubGhzPresetOok270Async /* narrow OOK => spectral res */
#define GRAB_AM_PEAK_DB     6.0f        /* peak clears BOTH neighbours by this => AM/OOK */

/* ---- modulation table (edited in Config; Left on Start opens Config) ----- */
typedef struct {
    const char*         name;   /* short label + filename suffix */
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

/* ---- frequency table (edited in Config) --------------------------------- */
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

/* Config defaults: 433.92/315/868.35/434.42 enabled; AM650/AM270/FM476 enabled. */
#define GRAB_FREQ_MASK_DEFAULT 0x000Fu
#define GRAB_MOD_MASK_DEFAULT  0x000Bu
#define GRAB_CFG_PATH  EXT_PATH("subghz/.fox_grabber.cfg")
#define GRAB_CFG_MAGIC 0x47524231u /* 'GRB1' */

/* ---- screens / stages --------------------------------------------------- */
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
    bool       header_pleasewait; /* "Please Wait!" vs "Press & Hold KeyFob!" */
    int        countdown;         /* 3..1 during pre-read countdown, else 0 */
    bool       exit_requested;
    bool       open_in_subghz;  /* Right on Result -> launch SubGHz at saved_name */

    uint32_t   locked_freq;
    int        locked_preset;     /* -1 none */
    uint32_t   freq_mask;         /* enabled GRAB_FREQS bits (Config) */
    uint32_t   mod_mask;          /* enabled GRAB_MODS bits  (Config) */
    int        cfg_cursor;        /* Config list cursor: freqs then mods */
    int        cfg_top;           /* Config list scroll offset */
    int        result_rc;         /* 0 ok, <0 error */
    char       saved_name[64];
} GrabApp;

/* ---- capture (RAW -> temp .sub), lifted from fox_subghz_io's fox_do_rx --- */
static bool grab_capture(
    const SubGhzDevice* device,
    uint32_t freq,
    FuriHalSubGhzPreset preset,
    const char* preset_name,
    uint32_t ms) {
    SubGhzEnvironment* env = subghz_environment_alloc();
    void* raw = subghz_protocol_decoder_raw_alloc(env);

    SubGhzWorker* worker = subghz_worker_alloc();
    subghz_worker_set_overrun_callback(
        worker, (SubGhzWorkerOverrunCallback)subghz_protocol_decoder_raw_reset);
    subghz_worker_set_pair_callback(
        worker, (SubGhzWorkerPairCallback)subghz_protocol_decoder_raw_feed);
    subghz_worker_set_context(worker, raw);

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
    subghz_protocol_decoder_raw_free(raw);
    subghz_environment_free(env);
    return ok;
}

/* ---- segment ONE frame out of the temp .sub and write it to out_path -----
 * Copies the temp's header verbatim (so Frequency/Preset/Protocol are exactly
 * what the proven RAW writer produced), then streams the RAW_Data values and
 * slices the run of edges between the first two inter-frame gaps = one press.
 * Returns 0 on success, <0 on error (-2 = no clean frame found).            */
static int grab_segment_and_save(const char* out_path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* in = file_stream_alloc(storage);
    Stream* out = file_stream_alloc(storage);
    FuriString* line = furi_string_alloc();
    int32_t* frame = malloc(sizeof(int32_t) * GRAB_MAX_EDGES);
    int fcount = 0;
    int frames = 0;         /* complete repeats captured (guards seen while collecting) */
    int last_guard_end = 0; /* fcount just after the most recent guard (whole-frame trim) */
    int state = 0; /* 0 seek-first-gap, 1 collecting, 2 target reached, 3 overflow */
    int rc = -1;

    do {
        if(!frame) break;
        if(!file_stream_open(in, GRAB_TMP_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) break;
        if(!file_stream_open(out, out_path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) break;

        while(stream_read_line(in, line)) {
            char* rd = strstr(furi_string_get_cstr(line), "RAW_Data:");
            if(!rd) {
                /* header line: copy verbatim (normalise the newline) */
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
                        /* keep the leading guard gap as frame[0]: Princeton and
                         * most OOK decoders must see a guard BEFORE the bits to
                         * leave their reset state, else they collect nothing. */
                        frame[fcount++] = d;
                        last_guard_end = fcount;
                        state = 1;
                    }
                } else if(state == 1) {
                    if(fcount < GRAB_MAX_EDGES) {
                        frame[fcount++] = d;
                    } else {
                        state = 3; /* out of room: stop, keep whole frames so far */
                        break;
                    }
                    if(is_gap) {
                        /* end of one repeat. Decoders (Princeton et al.) only emit
                         * after TWO identical frames, so keep several repeats. */
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

        /* keep whole frames only: drop any trailing partial after the last guard */
        if(last_guard_end > 0) fcount = last_guard_end;
        if(frames < GRAB_FRAMES_MIN || fcount < GRAB_MIN_EDGES) {
            rc = -2; /* not enough clean repeats -- no file written */
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
    if(rc != 0) storage_simply_remove(storage, out_path); /* no junk header-only .sub */
    furi_record_close(RECORD_STORAGE);
    return rc;
}

static void grab_set(GrabApp* app, GrabScreen screen, GrabStage stage) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->screen = screen;
    app->stage = stage;
    furi_mutex_release(app->mutex);
}

/* ---- Config persistence (tiny binary file on SD) ------------------------ */
static void grab_cfg_load(GrabApp* app) {
    app->freq_mask = GRAB_FREQ_MASK_DEFAULT;
    app->mod_mask  = GRAB_MOD_MASK_DEFAULT;
    Storage* st = furi_record_open(RECORD_STORAGE);
    File* f = storage_file_alloc(st);
    if(storage_file_open(f, GRAB_CFG_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
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

/* AM(OOK) presets vs FM(2FSK/MSK) — used to order the mod sweep from the
 * spectral AM/FM guess. */
static bool grab_mod_is_am(FuriHalSubGhzPreset p) {
    return p == FuriHalSubGhzPresetOok650Async || p == FuriHalSubGhzPresetOok270Async;
}

/* ---- worker: (phase 2) capture -> segment -> save ------------------------ */
static int32_t grab_worker(void* ctx) {
    GrabApp* app = ctx;

    /* Snapshot the enabled Config set (freqs + mods to test). */
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
        app->result_rc = -4; /* nothing enabled in Config */
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

    /* Reference preset for the freq sweep = first enabled modulation. */
    const FuriHalSubGhzPreset ref_preset = GRAB_MODS[mods[0]].preset;

    /* Sample the ambient noise floor on each enabled freq BEFORE the press
     * (during the countdown the fob is still off). */
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

    /* Countdown so the user knows when to press; buzz on each number. */
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

    /* Hold a vibrate + blink the RX LED while we sweep for the signal. The
     * vibrate STOPPING = "you can let go". */
    notification_message(app->notifications, &sequence_set_vibro_on);
    notification_message(app->notifications, &sequence_blink_start_cyan);

    /* FREQ sweep: each pass measure RSSI on every enabled freq and lock the one with the
     * STRONGEST margin over its own noise floor (not merely the first above it) = the real
     * transmitter, so a weak adjacent band can't win just by being earlier in the list. */
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

    int rc = -3; /* never saw the fob on any enabled freq */
    char path[96];
    if(fi >= 0) {
        /* FINE REFINE + AM/FM SPECTRAL: narrow-BW sweep +/-300 kHz / 20 kHz around
         * the coarse peak. Keep the fine peak as the capture frequency, and judge
         * AM(sharp spike) vs FM(broad) from the peak-vs-neighbour shape. */
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
            /* sharp peak (clears BOTH neighbours by >=GRAB_AM_PEAK_DB) => AM/OOK */
            if(peak_s > 0 && peak_s < GRAB_FINE_POINTS - 1 && fine_r[peak_s - 1] > -199.0f &&
               fine_r[peak_s + 1] > -199.0f) {
                float dl = fine_r[peak_s] - fine_r[peak_s - 1];
                float dr = fine_r[peak_s] - fine_r[peak_s + 1];
                is_am = (dl >= GRAB_AM_PEAK_DB && dr >= GRAB_AM_PEAK_DB);
                have_spectral = true;
            }
            subghz_devices_idle(device);
        }

        /* Order the modulation sweep: with a confident spectral guess, try that
         * family first (1-2 presets); otherwise keep the Config order. */
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

        /* MOD sweep: on the fine-locked freq, try the ordered modulations and keep
         * the FIRST that yields a clean, decodable burst = the winner. */
        DateTime dt;
        furi_hal_rtc_get_datetime(&dt);
        rc = -2; /* signal seen but no mod produced a clean frame */
        for(int k = 0; k < no; k++) {
            int m = order[k];
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            app->stage = GrabStageMod;
            app->header_pleasewait = false;
            furi_mutex_release(app->mutex);

            bool cap = grab_capture(
                device, cap_freq, GRAB_MODS[m].preset, GRAB_MODS[m].name, GRAB_CAPTURE_MS);
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
        app->stage = GrabStageDone;
    } else {
        app->stage = GrabStageError;
    }
    app->screen = GrabScreenResult;
    furi_mutex_release(app->mutex);
    return 0;
}

/* ---- draw ---------------------------------------------------------------- */
static const char* grab_stage_text(GrabStage s) {
    switch(s) {
    case GrabStageFreq:    return "Detecting Freq.";
    case GrabStageMod:     return "Detecting Modulation";
    case GrabStageRead:    return "Reading Signal";
    case GrabStageAnalyze: return "Analyzing Signal";
    default:               return "";
    }
}

static void draw_start(Canvas* c, GrabApp* app) {
    UNUSED(app);
    canvas_draw_icon(c, 2, 14, &I_fox_32x32);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 42, 26, "Press & Hold");
    canvas_draw_str(c, 42, 40, "KeyFob!");
    elements_button_left(c, "Config");
    elements_button_center(c, "Start");
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
        char buf[24]; /* -Werror=format-truncation: fit worst-case %d */
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
    /* crude phase-2 progress hint; real bar + Fox buttons land in the UI phase */
    uint8_t w = (app->stage == GrabStageAnalyze) ? 108 : 54;
    canvas_draw_frame(c, 10, 46, 108, 8);
    canvas_draw_box(c, 10, 46, w, 8);
}

static void draw_result(Canvas* c, GrabApp* app) {
    canvas_set_font(c, FontPrimary);
    if(app->result_rc == 0) {
        const char* base = strrchr(app->saved_name, '/');
        base = base ? base + 1 : app->saved_name;
        canvas_draw_str_aligned(c, 64, 8, AlignCenter, AlignTop, "Saved!");
        canvas_set_font(c, FontSecondary);
        canvas_draw_str_aligned(c, 64, 24, AlignCenter, AlignCenter, base);
        canvas_draw_str_aligned(c, 64, 37, AlignCenter, AlignCenter, "OK: grab another");
        elements_button_left(c, "Discard");
        elements_button_right(c, "Open");
    } else {
        const char* msg = (app->result_rc == -2) ? "No clean frame" :
                          (app->result_rc == -3) ? "No signal seen" :
                          (app->result_rc == -4) ? "Nothing enabled" : "Capture failed";
        const char* hint =
            (app->result_rc == -4) ? "Pick freqs + mods" :
            (app->result_rc == -3) ? "Hold fob / check freqs" : "Hold the fob longer";
        canvas_draw_str_aligned(c, 64, 10, AlignCenter, AlignTop, msg);
        canvas_set_font(c, FontSecondary);
        canvas_draw_str_aligned(c, 64, 30, AlignCenter, AlignCenter, hint);
        canvas_draw_str_aligned(c, 64, 56, AlignCenter, AlignBottom, "OK: retry   Back: exit");
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

/* ---- lifecycle ----------------------------------------------------------- */
static void grab_start_worker(GrabApp* app) {
    if(app->worker) return;
    app->header_pleasewait = false;
    app->result_rc = 0;
    app->countdown = 3;
    app->screen = GrabScreenProgress;
    app->stage = GrabStageRead;
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
    subghz_devices_init();
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
                    /* Config list: Up/Down move, OK toggles, Back saves + returns. */
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
                        grab_start_worker(app);
                    } else if(screen == GrabScreenResult) {
                        grab_set(app, GrabScreenStart, GrabStageIdle); /* grab another */
                    }
                } else if(screen == GrabScreenResult && rc == 0) {
                    if(event.key == InputKeyLeft) {
                        /* Discard: delete the saved .sub, back to Start. */
                        Storage* st = furi_record_open(RECORD_STORAGE);
                        storage_simply_remove(st, app->saved_name);
                        furi_record_close(RECORD_STORAGE);
                        grab_set(app, GrabScreenStart, GrabStageIdle);
                    } else if(event.key == InputKeyRight) {
                        /* Open the grab in SubGHz, then exit the grabber. */
                        app->open_in_subghz = true;
                        app->exit_requested = true;
                    }
                } else if(screen == GrabScreenStart && event.key == InputKeyLeft) {
                    /* Left opens Config (like SubGHz Read's Config button). */
                    furi_mutex_acquire(app->mutex, FuriWaitForever);
                    app->cfg_cursor = 0;
                    app->cfg_top = 0;
                    app->screen = GrabScreenConfig;
                    furi_mutex_release(app->mutex);
                }
            }
        }
        view_port_update(app->view_port);
    }

    bool open = app->open_in_subghz;
    char open_path[64];
    strncpy(open_path, app->saved_name, sizeof(open_path) - 1);
    open_path[sizeof(open_path) - 1] = '\0';

    grab_free(app);
    subghz_devices_deinit();

    if(open) {
        /* Hand the just-grabbed .sub to SubGHz (RAW -> Read RAW/send view). Use the launch
         * QUEUE, not a detached start: the loader is still locked by this FAP, so a direct
         * start is rejected and we would just fall back to the apps list. The queued launch
         * fires AFTER we exit (same mechanism FFB uses to open files). */
        Loader* loader = furi_record_open(RECORD_LOADER);
        loader_enqueue_launch(loader, "Sub-GHz", open_path, LoaderDeferredLaunchFlagGui);
        furi_record_close(RECORD_LOADER);
    }
    return 0;
}
