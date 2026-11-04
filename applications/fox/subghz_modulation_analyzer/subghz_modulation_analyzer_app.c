#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <gui/elements.h>
#include <input/input.h>
#include <loader/loader.h>
#include <storage/storage.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <lib/flipper_format/flipper_format.h>
#include <lib/subghz/subghz_setting.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

extern const uint8_t subghz_device_cc1101_preset_ook_270khz_async_regs[];
extern const uint8_t subghz_device_cc1101_preset_ook_650khz_async_regs[];
extern const uint8_t subghz_device_cc1101_preset_2fsk_dev2_38khz_async_regs[];
extern const uint8_t subghz_device_cc1101_preset_2fsk_dev12khz_async_regs[];
extern const uint8_t subghz_device_cc1101_preset_2fsk_dev47_6khz_async_regs[];
extern const uint8_t subghz_device_cc1101_preset_msk_99_97kb_async_regs[];
extern const uint8_t subghz_device_cc1101_preset_gfsk_9_99kb_async_regs[];

#define MA_MOD_FILTER_COUNT 64u
#define MA_MOD_FILTER_PATH "/ext/subghz/modulation_filter.save"

static uint8_t g_mod_filter[MA_MOD_FILTER_COUNT];
static bool    g_mod_filter_loaded = false;

static void ma_load_mod_filter(void) {
    memset(g_mod_filter, 0x01, sizeof(g_mod_filter));
    Storage* s = furi_record_open(RECORD_STORAGE);
    File*    f = storage_file_alloc(s);
    if(storage_file_open(f, MA_MOD_FILTER_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_read(f, g_mod_filter, sizeof(g_mod_filter));
    }
    storage_file_close(f);
    storage_file_free(f);
    furi_record_close(RECORD_STORAGE);
    g_mod_filter_loaded = true;
}

static bool ma_preset_enabled(uint8_t idx) {
    return (!g_mod_filter_loaded) || (idx >= MA_MOD_FILTER_COUNT) || (g_mod_filter[idx] != 0x00);
}
#define RSSI_MIN      (-97.0f)
#define RSSI_MAX      (-60.0f)
#define RSSI_SCALE    2.3f
#define TRIGGER_STEP  1.0f
#define NOISE_MARGIN_DEFAULT 8.0f  // trigger = noise_floor + this many dB (default)
#define NOISE_MARGIN_MIN     2.0f  // T-/+T adjust range for the margin
#define NOISE_MARGIN_MAX     30.0f
#define NF_ALPHA             0.15f // ambient noise-floor EMA rate (idle sweeps)
#define MAX_HISTORY   4
#define REDRAW_MS     80u

// Big-name box idle-cycle period: while there is no confident detection the box
// sweeps the candidate presets on a wall clock (independent of the measurement
// sweep) so the idle display reads at a comfortable ~0.5s/step.
#define ANIM_MS       350u

// ---------------------------------------------------------------------------
// Modulation Analyzer = "which exact preset does this signal demodulate best
// under?".  RSSI is PRE-demodulator, so it can only ever see raw carrier power
// and cannot tell AM270 from AM650 (same carrier, different RX filter) or one
// FSK deviation from another.  The only way to resolve the EXACT modulation is
// to actually demodulate the signal under each candidate preset and measure how
// clean the result is.  So the engine (ma_scan_thread) sweeps every enabled
// preset, reconfigures the CC1101 to it, captures the demodulated pulse train
// for DWELL_MS via furi_hal_subghz_start_async_rx(), and scores how "structured"
// that pulse train is:
//   - a WELL-MATCHED preset -> many pulses whose durations cluster into a few
//     discrete symbol widths, with little sub-symbol glitch noise => high score.
//   - a MISMATCHED preset   -> merged/clipped pulses (filter too narrow), or a
//     spray of random short glitches (too wide / wrong FSK deviation) => low.
// AM-vs-FM falls out for free: an FM burst produces almost no structured pulses
// under any AM/OOK slicer and vice-versa, so the right FAMILY always wins before
// the right member does.  The preset with the best score is the exact answer.
#define DWELL_MS        100u   // async-rx capture window per preset, per sweep step
#define SYM_MIN_US       80u   // shortest plausible data symbol (below = glitch)
#define SYM_MAX_US     8000u   // longest plausible data symbol (above = gap/idle)
#define GLITCH_US        60u   // durations under this are noise, not data
#define MIN_CLEAN         8u   // need at least this many clean pulses to score at all
#define MIN_LEADER_SCORE  4.0f // leader must beat this to be called a detection
#define TIE_FRAC          0.80f // same-family score >= this x best => method-2 tie-break band
#define SCORE_DECAY       0.5f // per-sweep max-hold decay (best capture in a press sticks)
#define RELEASE_SWEEPS    1u   // sweeps of no-signal before a press is committed to history
#define MA_HIST_BINS     14u   // log2 duration-histogram bins (peakedness measure)
#define MA_MAX_CAND      24u   // most presets we will sweep in one pass

#define MODANA_FREQ_FILE EXT_PATH("subghz/.modana_freq")
#define LAST_SETTINGS_PATH EXT_PATH("subghz/assets/last_subghz.settings")
#define LAST_SETTINGS_TYPE    "Flipper SubGhz Last Setting File"
#define LAST_SETTINGS_VERSION 3

static inline bool feq(float a, float b) { return fabsf(a-b) < 0.001f; }

typedef enum { ScreenFreqSetup, ScreenScanning } AppScreen;
typedef enum { FeedbackMute=0, FeedbackSound, FeedbackAll, FeedbackVibro, FeedbackCount } FeedbackLevel;

// Per-dwell demod-pulse accumulator. Written ONLY by the capture ISR
// (ma_capture_cb) between start_async_rx() and stop_async_rx(); read by the scan
// thread after stop_async_rx() (a barrier), so no lock is needed - but the
// fields are volatile because the ISR and thread touch them on different sides
// of that barrier.
typedef struct {
    volatile uint32_t edges;              // every edge the slicer produced
    volatile uint32_t clean;              // edges with a plausible symbol duration
    volatile uint32_t glitch;             // edges shorter than GLITCH_US (noise)
    volatile uint16_t hist[MA_HIST_BINS]; // log2 histogram of clean durations
} MaDwellStat;

// One swept candidate preset and its rolling quality score.
typedef struct {
    uint8_t preset_idx;
    float   score;       // max-hold-with-decay across sweeps (the ranking value)
    float   last_score;  // most recent single-dwell score (diagnostic/display)
    float   rssi;        // RSSI sampled during its last dwell
} MaCand;

typedef struct {
    Gui*              gui;
    ViewPort*         view_port;
    FuriMessageQueue* input_queue;
    FuriMutex*        mutex;
    NotificationApp*  notifications;
    FuriThread*       scan_thread;

    AppScreen  screen;

    uint8_t    freq_digits[5];
    uint8_t    cursor;

    uint32_t   scan_freq;
    uint8_t    preset_idx;      // freq-setup default / mod-filter skip only
    float      current_rssi;
    float      rssi_last;
    float      trigger;         // manual margin (dB) above the noise floor
    float      noise_floor;     // auto-tracked ambient RSSI baseline (EMA, idle)
    uint8_t    confidence;      // 0-100: how clearly leader beat other families

    // Demod-sweep engine state.
    bool       scan_stop;        // tells the worker thread to exit (app teardown)
    MaDwellStat dwell;           // current capture accumulator (ISR writes)
    MaCand     cand[MA_MAX_CAND];
    uint8_t    ncand;
    uint8_t    testing_slot;     // candidate currently being measured (idle animation)
    int8_t     leader_slot;      // best candidate, or -1 when none is confident
    int8_t     runner_slot;      // same-family runner-up for alternation, or -1
    bool       signal_present;   // a confident detection exists this sweep
    bool       press_active;     // a held press is in progress (for 1-commit-per-press)
    uint8_t    press_leader;     // preset idx latched while the press was strongest
    uint8_t    no_signal_sweeps; // consecutive no-signal sweeps (release debounce)

    uint8_t    history[MAX_HISTORY];
    uint8_t    history_count[MAX_HISTORY];
    uint8_t    history_len;
    uint8_t    selected_index;
    bool       show_frame;

    FeedbackLevel feedback;
    bool       exit_requested;
    bool       ok_result_selected;
    SubGhzSetting* setting;
    uint8_t    preset_count;
} ModAnalApp;

static uint32_t digits_to_hz(const uint8_t* d) {
    uint32_t mhz = (uint32_t)d[0]*100 + d[1]*10 + d[2];
    uint32_t khz = (uint32_t)d[3]*10  + d[4];
    return mhz * 1000000u + khz * 10000u;
}

static void hz_to_digits(uint32_t hz, uint8_t* d) {
    uint32_t mhz = hz / 1000000u;
    uint32_t rem = (hz % 1000000u) / 10000u;
    d[0] = (uint8_t)(mhz / 100 % 10);
    d[1] = (uint8_t)(mhz / 10  % 10);
    d[2] = (uint8_t)(mhz       % 10);
    d[3] = (uint8_t)(rem / 10  % 10);
    d[4] = (uint8_t)(rem       % 10);
}

static void save_freq(ModAnalApp* app) {
    Storage* s = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(s);
    if(flipper_format_file_open_existing(ff, LAST_SETTINGS_PATH)) {
        uint32_t freq_hz = digits_to_hz(app->freq_digits);
        if(!flipper_format_update_uint32(ff, "Frequency", &freq_hz, 1)) {
            flipper_format_write_uint32(ff, "Frequency", &freq_hz, 1);
        }
    }
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
}

static void load_freq(ModAnalApp* app) {
    hz_to_digits(433920000u, app->freq_digits);
    Storage* s = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(s);
    if(flipper_format_file_open_existing(ff, LAST_SETTINGS_PATH)) {
        uint32_t version = 0;
        FuriString* type = furi_string_alloc();
        if(flipper_format_read_header(ff, type, &version) &&
           furi_string_equal_str(type, LAST_SETTINGS_TYPE) &&
           version == LAST_SETTINGS_VERSION) {
            uint32_t freq_hz = 0;
            if(flipper_format_read_uint32(ff, "Frequency", &freq_hz, 1) && freq_hz > 0) {
                hz_to_digits(freq_hz, app->freq_digits);
            }
        }
        furi_string_free(type);
    }
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
}

static void load_trigger(ModAnalApp* app) {
    Storage* s = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(s);
    if(flipper_format_file_open_existing(ff, LAST_SETTINGS_PATH)) {
        uint32_t version = 0;
        FuriString* type = furi_string_alloc();
        if(flipper_format_read_header(ff, type, &version) &&
           furi_string_equal_str(type, LAST_SETTINGS_TYPE) &&
           version == LAST_SETTINGS_VERSION) {
            float trigger = NOISE_MARGIN_DEFAULT;
            if(flipper_format_read_float(ff, "FAMargin", &trigger, 1)) {
                if(trigger < NOISE_MARGIN_MIN) trigger = NOISE_MARGIN_MIN;
                if(trigger > NOISE_MARGIN_MAX) trigger = NOISE_MARGIN_MAX;
                app->trigger = trigger;
            }
            flipper_format_rewind(ff);
            uint32_t fb = 0;
            if(flipper_format_read_uint32(ff, "FeedbackLevel", &fb, 1)) {
                app->feedback = (FeedbackLevel)(fb < (uint32_t)FeedbackCount ? fb : 0);
            }
            flipper_format_rewind(ff);
            uint32_t pi = 0;
            if(flipper_format_read_uint32(ff, "FAPresetIndex", &pi, 1)) {
                if(pi < (uint32_t)app->preset_count) app->preset_idx = (uint8_t)pi;
            }
        }
        furi_string_free(type);
    }
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
}

static void save_trigger(ModAnalApp* app) {
    Storage* s = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(s);
    if(flipper_format_file_open_existing(ff, LAST_SETTINGS_PATH)) {
        if(!flipper_format_update_float(ff, "FAMargin", &app->trigger, 1)) {
            flipper_format_write_float(ff, "FAMargin", &app->trigger, 1);
        }
        flipper_format_rewind(ff);
        uint32_t fb = (uint32_t)app->feedback;
        if(!flipper_format_update_uint32(ff, "FeedbackLevel", &fb, 1)) {
            flipper_format_write_uint32(ff, "FeedbackLevel", &fb, 1);
        }
        flipper_format_rewind(ff);
        uint32_t pi = (uint32_t)app->preset_idx;
        if(!flipper_format_update_uint32(ff, "FAPresetIndex", &pi, 1)) {
            flipper_format_write_uint32(ff, "FAPresetIndex", &pi, 1);
        }
    }
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
}

static void write_result_to_settings(ModAnalApp* app, uint32_t freq_hz, uint8_t preset_idx) {
    Storage* s = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(s);
    if(flipper_format_file_open_existing(ff, LAST_SETTINGS_PATH)) {
        if(!flipper_format_update_uint32(ff, "Frequency", &freq_hz, 1)) {
            flipper_format_write_uint32(ff, "Frequency", &freq_hz, 1);
        }

        flipper_format_rewind(ff);
        FuriString* preset_str = furi_string_alloc_set(
            subghz_setting_get_preset_name(app->setting, preset_idx));
        if(!flipper_format_update_string(ff, "Preset", preset_str)) {
            flipper_format_write_string(ff, "Preset", preset_str);
        }
        furi_string_free(preset_str);
    }
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
}

// Commit one resolved preset index into the rolling history (one entry/press).
static void ma_commit_history(ModAnalApp* app, uint8_t nf) {
    bool in_arr = false;
    for(uint8_t i = 0; i < MAX_HISTORY; i++) {
        if(app->history_count[i] > 0 && app->history[i] == nf) {
            in_arr = true;
            app->history_count[i]++;
            if(i > 0) {
                uint8_t tc = app->history_count[i];
                for(uint8_t j = MAX_HISTORY - 1; j > 0; j--) {
                    uint8_t off = (j >= i) ? 1 : 0;
                    app->history[j] = app->history[j - off];
                    app->history_count[j] = app->history_count[j - off];
                }
                app->history[0] = nf;
                app->history_count[0] = tc;
            }
            break;
        }
    }
    if(!in_arr) {
        app->history[3] = app->history[2];
        app->history[2] = app->history[1];
        app->history[1] = app->history[0];
        app->history[0] = nf;
        app->history_count[3] = app->history_count[2];
        app->history_count[2] = app->history_count[1];
        app->history_count[1] = app->history_count[0];
        app->history_count[0] = 1;
    }
    app->history_len = 0;
    for(uint8_t i = 0; i < MAX_HISTORY; i++)
        if(app->history_count[i] > 0) app->history_len = i + 1;
    app->show_frame = (app->history_len > 0);
}

// ---- Demod-quality scoring ------------------------------------------------

// Capture ISR: called from the TIM2 input-capture interrupt for every edge of
// the demodulated data line while async RX is running. MUST stay tiny and
// integer-only (no floats, no SPI, no blocking). It only bins the edge.
static void ma_capture_cb(bool level, uint32_t duration, void* ctx) {
    UNUSED(level);
    MaDwellStat* d = ctx;
    d->edges++;
    if(duration < GLITCH_US) {
        d->glitch++;
        return;
    }
    if(duration >= SYM_MIN_US && duration <= SYM_MAX_US) {
        d->clean++;
        uint32_t v = duration / SYM_MIN_US; // >= 1
        uint32_t b = 0;
        while(v > 1 && b < MA_HIST_BINS - 1) {
            v >>= 1;
            b++;
        }
        d->hist[b]++;
    }
}

// Turn one dwell's accumulated pulse stats into a single "how cleanly did this
// preset demodulate" score. Rewards many clean pulses that concentrate into a
// couple of histogram bins (sharp symbol clustering) and punishes glitch noise.
static float ma_dwell_score(const MaDwellStat* d) {
    uint32_t clean = d->clean;
    if(clean < MIN_CLEAN) return 0.0f;

    uint32_t top1 = 0, top2 = 0; // two most-populated duration bins
    for(uint32_t i = 0; i < MA_HIST_BINS; i++) {
        uint32_t h = d->hist[i];
        if(h >= top1) {
            top2 = top1;
            top1 = h;
        } else if(h > top2) {
            top2 = h;
        }
    }
    float peak = (float)(top1 + top2) / (float)clean;                 // 0..1 clustering
    float noise = (d->edges > 0) ? (float)d->glitch / (float)d->edges : 1.0f;
    float score = (float)clean * peak * (1.0f - noise);
    return (score > 0.0f) ? score : 0.0f;
}

// The sweep worker. Owns the radio while scanning: for each enabled preset it
// reconfigures the CC1101, captures DWELL_MS of demodulated pulses, scores them,
// and (per sweep) elects the best-scoring preset as the current modulation. The
// GUI thread never touches the radio, so there is no start/stop race.
// --- Method-2 selectivity helpers: parse a preset NAME ("AM270", "FM238",
// "FM12K") into a family (leading letters) and a tightness value (trailing
// number; a trailing K/k multiplies by 1000, so FM12K ranks WIDER than FM476).
// Smaller tightness = narrower RX bandwidth / smaller FSK deviation = the more
// selective, "tighter" match. Only ever compared within one family, so the
// AM-vs-FM number-scale mismatch never matters. ---
static uint32_t ma_preset_tightness(const char* name) {
    if(!name) return 0xFFFFFFFFu;
    const char* p = name;
    while(*p && (*p < '0' || *p > '9')) p++;
    uint32_t v = 0;
    while(*p >= '0' && *p <= '9') { v = v * 10u + (uint32_t)(*p - '0'); p++; }
    if(*p == 'K' || *p == 'k') v *= 1000u;
    return v;
}

static bool ma_same_family(const char* a, const char* b) {
    if(!a || !b) return false;
    for(;;) {
        char ca = *a, cb = *b;
        bool ad = (ca == '\0') || (ca >= '0' && ca <= '9');
        bool bd = (cb == '\0') || (cb >= '0' && cb <= '9');
        if(ad || bd) return ad && bd;
        if(ca >= 'a' && ca <= 'z') ca -= 32;
        if(cb >= 'a' && cb <= 'z') cb -= 32;
        if(ca != cb) return false;
        a++; b++;
    }
}

static int32_t ma_scan_thread(void* ctx) {
    ModAnalApp* app = ctx;
    bool was_scanning = false;

    while(true) {
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        bool stop = app->scan_stop;
        bool scanning = (app->screen == ScreenScanning) && !app->exit_requested;
        uint32_t freq = app->scan_freq;
        uint8_t ncand = app->ncand;
        float trigger = app->trigger;
        FeedbackLevel feedback = app->feedback;
        furi_mutex_release(app->mutex);

        if(stop) break;

        if(!scanning || ncand == 0 || freq == 0) {
            if(was_scanning) {
                furi_hal_subghz_idle();
                was_scanning = false;
            }
            furi_delay_ms(20);
            continue;
        }
        was_scanning = true;

        float sweep_max_rssi = RSSI_MIN;

        for(uint8_t slot = 0; slot < ncand; slot++) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            bool abort = app->scan_stop || app->screen != ScreenScanning || app->exit_requested;
            uint8_t pidx = app->cand[slot].preset_idx;
            app->testing_slot = slot;
            furi_mutex_release(app->mutex);
            if(abort) break;

            uint8_t* pdata = subghz_setting_get_preset_data(app->setting, pidx);
            if(pdata == NULL) continue;

            memset((void*)&app->dwell, 0, sizeof(app->dwell));
            furi_hal_subghz_idle();
            furi_hal_subghz_load_custom_preset(pdata);
            furi_hal_subghz_set_frequency_and_path(freq);
            furi_hal_subghz_start_async_rx(ma_capture_cb, (void*)&app->dwell);
            furi_delay_ms(DWELL_MS / 2);
            float rssi = furi_hal_subghz_get_rssi();
            furi_delay_ms(DWELL_MS - DWELL_MS / 2);
            furi_hal_subghz_stop_async_rx();

            float s = ma_dwell_score((const MaDwellStat*)&app->dwell);
            if(rssi > sweep_max_rssi) sweep_max_rssi = rssi;

            furi_mutex_acquire(app->mutex, FuriWaitForever);
            app->cand[slot].last_score = s;
            app->cand[slot].rssi = rssi;
            float held = app->cand[slot].score * SCORE_DECAY;
            app->cand[slot].score = (s > held) ? s : held;
            app->current_rssi = rssi;
            furi_mutex_release(app->mutex);
        }

        // End of sweep: elect a leader and run the per-press commit edge.
        bool play_feedback = false;
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        int best = -1;
        float bestv = 0.0f;
        for(uint8_t i = 0; i < app->ncand; i++) {
            if(app->cand[i].score > bestv) {
                bestv = app->cand[i].score;
                best = i;
            }
        }
        // Method 2 (selectivity cross-check): among SAME-family presets that
        // also decoded well (score >= TIE_FRAC * best), the signal fits the
        // tighter filter too, so the tightest becomes the leader; a wider
        // same-family sibling that also decoded is kept as the runner-up for
        // the ambiguity (alternation) display.
        int leader = best;
        int runner = -1;
        if(best >= 0) {
            const char* bname =
                subghz_setting_get_preset_name(app->setting, app->cand[best].preset_idx);
            uint32_t lead_tight = ma_preset_tightness(bname);
            for(uint8_t i = 0; i < app->ncand; i++) {
                if((int)i == best) continue;
                if(app->cand[i].score < TIE_FRAC * bestv) continue;
                const char* iname =
                    subghz_setting_get_preset_name(app->setting, app->cand[i].preset_idx);
                if(!ma_same_family(bname, iname)) continue;
                uint32_t it = ma_preset_tightness(iname);
                if(it < lead_tight) { lead_tight = it; leader = (int)i; }
            }
            const char* lname =
                subghz_setting_get_preset_name(app->setting, app->cand[leader].preset_idx);
            float rbest = 0.0f;
            for(uint8_t i = 0; i < app->ncand; i++) {
                if((int)i == leader) continue;
                if(app->cand[i].score < TIE_FRAC * bestv) continue;
                const char* iname =
                    subghz_setting_get_preset_name(app->setting, app->cand[i].preset_idx);
                if(!ma_same_family(lname, iname)) continue;
                if(app->cand[i].score > rbest) { rbest = app->cand[i].score; runner = (int)i; }
            }
        }
        bool present = (sweep_max_rssi > (app->noise_floor + trigger)) && (best >= 0) && (bestv >= MIN_LEADER_SCORE);
        app->leader_slot = present ? (int8_t)leader : -1;
        app->runner_slot = present ? (int8_t)runner : -1;
        app->signal_present = present;
        app->rssi_last = sweep_max_rssi;
        // Auto-calibrate the ambient noise floor from IDLE sweeps only (never
        // while a signal is present, or the floor would climb to the signal).
        if(!present) {
            if(app->noise_floor <= RSSI_MIN) app->noise_floor = sweep_max_rssi;
            else app->noise_floor += (sweep_max_rssi - app->noise_floor) * NF_ALPHA;
            if(app->noise_floor < RSSI_MIN) app->noise_floor = RSSI_MIN;
        }
        // Confidence: how clearly the leader beat the best OTHER-family preset.
        uint8_t conf = 0;
        if(present && leader >= 0) {
            const char* lfam =
                subghz_setting_get_preset_name(app->setting, app->cand[leader].preset_idx);
            float other_best = 0.0f;
            for(uint8_t i = 0; i < app->ncand; i++) {
                if((int)i == leader) continue;
                const char* ni =
                    subghz_setting_get_preset_name(app->setting, app->cand[i].preset_idx);
                if(ma_same_family(lfam, ni)) continue;
                if(app->cand[i].score > other_best) other_best = app->cand[i].score;
            }
            float ls = app->cand[leader].score;
            if(ls > 0.0f) {
                float c = (ls - other_best) / ls * 100.0f;
                if(c < 0.0f) c = 0.0f;
                if(c > 100.0f) c = 100.0f;
                conf = (uint8_t)(c + 0.5f);
            }
        }
        app->confidence = conf;

        if(present) {
            if(!app->press_active) play_feedback = true; // rising edge of a press
            app->press_active = true;
            app->press_leader = (leader >= 0) ? app->cand[leader].preset_idx : 0;
            app->no_signal_sweeps = 0;
        } else if(app->press_active) {
            app->no_signal_sweeps++;
            if(app->no_signal_sweeps >= RELEASE_SWEEPS) {
                ma_commit_history(app, app->press_leader);
                app->press_active = false;
                for(uint8_t i = 0; i < app->ncand; i++) {
                    app->cand[i].score = 0.0f;
                    app->cand[i].last_score = 0.0f;
                }
            }
        }
        furi_mutex_release(app->mutex);

        if(play_feedback && feedback != FeedbackMute) {
            if(feedback == FeedbackSound) {
                static const NotificationSequence seq_sound_only = {
                    &message_note_c5,
                    &message_delay_50,
                    &message_sound_off,
                    NULL,
                };
                notification_message(app->notifications, &seq_sound_only);
            } else if(feedback == FeedbackAll) {
                notification_message(app->notifications, &sequence_success);
            } else if(feedback == FeedbackVibro) {
                notification_message(app->notifications, &sequence_single_vibro);
            }
        }
    }

    furi_hal_subghz_idle();
    return 0;
}

static void draw_feedback_icon(Canvas* canvas, uint8_t x, uint8_t y, FeedbackLevel level) {
    canvas_set_color(canvas, ColorBlack);
    if(level == FeedbackMute) {
        canvas_draw_circle(canvas, x+4, y+3, 3);
        canvas_draw_line(canvas, x+1, y+6, x+7, y+0);
    } else if(level == FeedbackVibro) {
        canvas_draw_dot(canvas, x+0, y+3);
        canvas_draw_line(canvas, x+1, y+2, x+2, y+1);
        canvas_draw_line(canvas, x+3, y+1, x+4, y+2);
        canvas_draw_line(canvas, x+4, y+2, x+5, y+3);
        canvas_draw_line(canvas, x+5, y+3, x+6, y+4);
        canvas_draw_line(canvas, x+6, y+4, x+7, y+5);
        canvas_draw_line(canvas, x+7, y+5, x+8, y+4);
        canvas_draw_dot(canvas, x+9, y+3);
    } else {
        canvas_draw_box(canvas, x+0, y+2, 3, 3);
        canvas_draw_line(canvas, x+3, y+1, x+5, y+0);
        canvas_draw_line(canvas, x+3, y+4, x+5, y+6);
        canvas_draw_line(canvas, x+5, y+0, x+5, y+6);
        canvas_draw_line(canvas, x+4, y+1, x+4, y+5);
        if(level == FeedbackAll) {
            canvas_draw_line(canvas, x+7, y+1, x+8, y+2);
            canvas_draw_line(canvas, x+8, y+2, x+8, y+4);
            canvas_draw_line(canvas, x+8, y+4, x+7, y+5);
        }
    }
}

static void draw_rssi(Canvas* c, float rssi, float rssi_last, float trigger, uint8_t x, uint8_t y) {
    if(!feq(rssi, 0.f)) {
        if(rssi > RSSI_MAX) rssi = RSSI_MAX;
        rssi = (rssi - RSSI_MIN) / RSSI_SCALE;
        uint8_t col = 0;
        for(size_t i = 0; i <= (uint8_t)rssi; i++) {
            if((i+1)%4) { col++; canvas_draw_box(c, x+2*i, (y+4)-col, 2, col); }
        }
    }
    if(!feq(rssi_last, 0.f)) {
        if(rssi_last > RSSI_MAX) rssi_last = RSSI_MAX;
        int mx = (int)((rssi_last-RSSI_MIN)/RSSI_SCALE)*2;
        int mh = (int)((rssi_last-RSSI_MIN)/RSSI_SCALE)+1;
        mh -= (mh/4)+3;
        canvas_draw_line(c, x+mx+1, y-mh, x+mx+1, y+3);
    }
    float tr = (trigger-RSSI_MIN)/RSSI_SCALE;
    uint8_t tx = (uint8_t)((float)x + 2*tr);
    canvas_draw_dot(c, tx, y+4);
    canvas_draw_line(c, tx-1, y+5, tx+1, y+5);
    canvas_draw_line(c, x, y+3, x+(uint8_t)((RSSI_MAX-RSSI_MIN)*2/RSSI_SCALE), y+3);
}

static void draw_freq_setup(Canvas* canvas, ModAnalApp* app) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 6, AlignCenter, AlignTop, "Set Frequency");

    const uint8_t digit_w = 13;
    const uint8_t dot_w   = 6;
    const uint8_t start_x = 17;
    const uint8_t digit_y = 40;

    canvas_set_font(canvas, FontBigNumbers);
    uint8_t x = start_x;
    for(uint8_t i = 0; i < 5; i++) {
        if(i == 3) {
            canvas_set_font(canvas, FontPrimary);
            canvas_draw_str(canvas, x, digit_y, ".");
            x += dot_w;
            canvas_set_font(canvas, FontBigNumbers);
        }
        char ch[2] = {'0' + app->freq_digits[i], 0};
        canvas_draw_str(canvas, x, digit_y, ch);
        if(i == app->cursor) {
            canvas_draw_box(canvas, x-1, digit_y+2, digit_w-1, 2);
        }
        x += digit_w;
    }
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, x+2, digit_y-2, "MHz");

    elements_button_center(canvas, "Continue");
}

static void draw_scanning(Canvas* canvas, ModAnalApp* app) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);

    canvas_draw_str(canvas, 17, 7, "Modulation Analyzer");

    draw_feedback_icon(canvas, 117, 0, app->feedback);

    // Big name box: a confident detection shows the WINNING preset (inverted);
    // otherwise the box idle-cycles the candidates on a ~0.5s wall clock so the
    // analyzer always looks alive and legible. The small number is that preset's
    // demod-quality score ("q"), which is what the engine actually ranks on - a
    // live readout for tuning and for judging confidence.
    uint8_t disp_idx = app->preset_idx;
    bool    is_inverted = false;

    if(app->signal_present && app->leader_slot >= 0 && app->leader_slot < (int8_t)app->ncand) {
        int8_t show = app->leader_slot;
        // Genuine same-family ambiguity: a runner-up that also decoded well
        // exists, so alternate leader/runner every ANIM_MS to read e.g.
        // "FM238 / FM476" = "it's one of these two" (hardware can't split them).
        if(app->runner_slot >= 0 && app->runner_slot < (int8_t)app->ncand) {
            uint32_t at = furi_ms_to_ticks(ANIM_MS);
            if(at == 0) at = 1;
            if((furi_get_tick() / at) & 1u) show = app->runner_slot;
        }
        disp_idx = app->cand[show].preset_idx;
        is_inverted = true;
    } else if(app->ncand > 0) {
        uint32_t at = furi_ms_to_ticks(ANIM_MS);
        if(at == 0) at = 1;
        uint8_t slot = (uint8_t)((furi_get_tick() / at) % app->ncand);
        disp_idx = app->cand[slot].preset_idx;
        is_inverted = false;
    }

    const char* preset_display_name =
        subghz_setting_get_preset_name(app->setting, disp_idx);

    if(is_inverted) {
        canvas_draw_box(canvas, 4, 10, 75, 19);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_set_color(canvas, ColorBlack);
    }

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 8, 20, preset_display_name);
    {
        // Live signal level (dBm) in the box, replacing the old raw q score.
        canvas_set_font(canvas, FontSecondary);
        float box_dbm = app->signal_present ? app->rssi_last : app->current_rssi;
        if(!feq(box_dbm, 0.0f)) {
            char db[10];
            snprintf(db, sizeof(db), "%ddB", (int)(box_dbm + (box_dbm < 0.0f ? -0.5f : 0.5f)));
            canvas_draw_str(canvas, 49, 20, db);
        }
    }
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontSecondary);
    char fstr[12];
    uint32_t f = app->scan_freq;
    snprintf(fstr, sizeof(fstr), "%lu.%02lu",
        (unsigned long)(f/1000000u),
        (unsigned long)((f%1000000u)/10000u));
    canvas_draw_str(canvas, 90, 16, fstr);
    canvas_draw_str(canvas, 90, 25, "MHz");

    const uint8_t x1=2, x2=66, y=37;
    uint8_t line = 0;
    bool show_frame = app->show_frame && app->history_len > 0;
    for(uint8_t i = 0; i < MAX_HISTORY; i++) {
        uint8_t cx = (i%2==0) ? x1 : x2;
        uint8_t cy = y + line*11;
        if(i%2 != 0) line++;
        uint8_t hidx = app->history[i];
        const char* name = (app->history_count[i]>0 && hidx < app->preset_count)
            ? subghz_setting_get_preset_name(app->setting, hidx) : "------";
        canvas_draw_str(canvas, cx, cy, name);
        if(app->history_count[i] > 1) {
            char rx[8]; snprintf(rx, sizeof(rx), "x%d", app->history_count[i]);
            canvas_draw_str(canvas, cx+41, cy, rx);
        }
        if(show_frame && i==app->selected_index)
            elements_frame(canvas, cx-2, cy-9, 63, 11);
    }

    if(app->signal_present && app->confidence > 0) {
        char cf[8];
        snprintf(cf, sizeof(cf), "%d%%", app->confidence);
        canvas_draw_str(canvas, 2, 62, cf);
    }
    canvas_draw_str(canvas, 33, 62, "RSSI");
    float rssi = app->signal_present ? app->rssi_last : app->current_rssi;
    draw_rssi(canvas, rssi, app->rssi_last, app->noise_floor + app->trigger, 56, 57);
    elements_button_left(canvas, "T-");
    elements_button_right(canvas, "+T");
}

static void draw_cb(Canvas* canvas, void* ctx) {
    ModAnalApp* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    if(app->screen == ScreenFreqSetup) {
        draw_freq_setup(canvas, app);
    } else {
        draw_scanning(canvas, app);
    }
    furi_mutex_release(app->mutex);
}

static void input_cb(InputEvent* event, void* ctx) {
    furi_message_queue_put((FuriMessageQueue*)ctx, event, FuriWaitForever);
}

static ModAnalApp* app_alloc(void) {
    ModAnalApp* app = malloc(sizeof(ModAnalApp));
    memset(app, 0, sizeof(*app));
    app->mutex       = furi_mutex_alloc(FuriMutexTypeNormal);
    app->trigger     = NOISE_MARGIN_DEFAULT;
    app->noise_floor = RSSI_MIN;
    app->feedback    = FeedbackMute;
    app->screen      = ScreenFreqSetup;
    app->leader_slot = -1;
    app->runner_slot = -1;
    load_freq(app);
    load_trigger(app);

    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->gui         = furi_record_open(RECORD_GUI);
    app->view_port   = view_port_alloc();
    app->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    app->setting = subghz_setting_alloc();
    subghz_setting_load(app->setting, EXT_PATH("subghz/assets/setting_user.file"));
    app->preset_count = (uint8_t)subghz_setting_get_preset_count(app->setting);
    ma_load_mod_filter();

    while(g_mod_filter_loaded && g_mod_filter[app->preset_idx] == 0x00
          && app->preset_idx < app->preset_count - 1u)
        app->preset_idx++;

    view_port_draw_callback_set(app->view_port, draw_cb, app);
    view_port_input_callback_set(app->view_port, input_cb, app->input_queue);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
    view_port_update(app->view_port);

    // One sweep worker for the app lifetime; it idles while on the setup screen.
    app->scan_thread = furi_thread_alloc_ex("MaScan", 2048, ma_scan_thread, app);
    furi_thread_start(app->scan_thread);

    return app;
}

// Build the candidate set (every enabled preset) and reset the engine, then hand
// the radio to the worker by switching to the scanning screen.
static void start_scanning(ModAnalApp* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->scan_freq = digits_to_hz(app->freq_digits);

    app->ncand = 0;
    for(uint8_t i = 0; i < app->preset_count && app->ncand < MA_MAX_CAND; i++) {
        if(!ma_preset_enabled(i)) continue;
        app->cand[app->ncand].preset_idx = i;
        app->cand[app->ncand].score = 0.0f;
        app->cand[app->ncand].last_score = 0.0f;
        app->cand[app->ncand].rssi = RSSI_MIN;
        app->ncand++;
    }
    if(app->ncand == 0) { // nothing enabled: fall back to preset 0 so the app still runs
        app->cand[0].preset_idx = 0;
        app->cand[0].score = 0.0f;
        app->cand[0].last_score = 0.0f;
        app->cand[0].rssi = RSSI_MIN;
        app->ncand = 1;
    }

    app->testing_slot = 0;
    app->leader_slot = -1;
    app->runner_slot = -1;
    app->signal_present = false;
    app->press_active = false;
    app->press_leader = 0;
    app->no_signal_sweeps = 0;
    app->current_rssi = 0;
    app->rssi_last = 0;
    app->screen = ScreenScanning;
    furi_mutex_release(app->mutex);
}

// Hand the radio back: the worker sees the screen change, finishes its current
// dwell and idles the radio itself (the GUI thread must not touch the radio).
static void stop_scanning(ModAnalApp* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->screen = ScreenFreqSetup;
    app->signal_present = false;
    app->leader_slot = -1;
    app->runner_slot = -1;
    furi_mutex_release(app->mutex);
}

int32_t subghz_modulation_analyzer_app(void* p) {
    char return_marker[24] = {0};
    bool launched_from_garage_menu = false;
    if(p && ((const char*)p)[0]) {
        const char* arg = (const char*)p;
        if(strncmp(arg, "gmenu:", 6) == 0) {
            launched_from_garage_menu = true;
            arg += 6;
        } else if(strncmp(arg, "core:", 5) == 0) {
            arg += 5;
        }
        strncpy(return_marker, arg, sizeof(return_marker) - 1);
    }

    furi_hal_subghz_reset();
    furi_hal_subghz_idle();

    ModAnalApp* app = app_alloc();

    InputEvent event;
    while(!app->exit_requested) {
        if(furi_message_queue_get(app->input_queue, &event, REDRAW_MS) == FuriStatusOk) {
            if(app->screen == ScreenFreqSetup) {
                if(event.key == InputKeyBack && event.type == InputTypeShort) {
                    app->exit_requested = true;
                } else if(event.key == InputKeyOk && event.type == InputTypeShort) {
                    uint32_t candidate = digits_to_hz(app->freq_digits);
                    if(furi_hal_subghz_is_frequency_valid(candidate)) {
                        save_freq(app);
                        start_scanning(app);
                    }
                } else if((event.key == InputKeyLeft || event.key == InputKeyRight) &&
                           (event.type == InputTypeShort || event.type == InputTypeRepeat)) {
                    furi_mutex_acquire(app->mutex, FuriWaitForever);
                    if(event.key == InputKeyLeft) {
                        if(app->cursor > 0) app->cursor--;
                    } else {
                        if(app->cursor < 4) app->cursor++;
                    }
                    furi_mutex_release(app->mutex);
                } else if((event.key == InputKeyUp || event.key == InputKeyDown) &&
                           (event.type == InputTypeShort || event.type == InputTypeRepeat)) {
                    furi_mutex_acquire(app->mutex, FuriWaitForever);
                    uint8_t* d = &app->freq_digits[app->cursor];
                    if(event.key == InputKeyUp) {
                        *d = (*d + 1) % 10;
                    } else {
                        *d = (*d == 0) ? 9 : (*d - 1);
                    }
                    furi_mutex_release(app->mutex);
                }
            } else {
                if(event.key == InputKeyBack && event.type == InputTypeShort) {
                    stop_scanning(app);
                } else if(event.key == InputKeyLeft &&
                          (event.type == InputTypePress || event.type == InputTypeRepeat)) {
                    furi_mutex_acquire(app->mutex, FuriWaitForever);
                    app->trigger -= TRIGGER_STEP;
                    if(app->trigger < NOISE_MARGIN_MIN) app->trigger = NOISE_MARGIN_MIN;
                    furi_mutex_release(app->mutex);
                } else if(event.key == InputKeyRight &&
                          (event.type == InputTypePress || event.type == InputTypeRepeat)) {
                    furi_mutex_acquire(app->mutex, FuriWaitForever);
                    app->trigger += TRIGGER_STEP;
                    if(app->trigger > NOISE_MARGIN_MAX) app->trigger = NOISE_MARGIN_MAX;
                    furi_mutex_release(app->mutex);
                } else if(event.key == InputKeyUp && event.type == InputTypePress) {
                    furi_mutex_acquire(app->mutex, FuriWaitForever);
                    app->feedback = (FeedbackLevel)((app->feedback+1) % FeedbackCount);
                    furi_mutex_release(app->mutex);
                } else if(event.key == InputKeyDown &&
                          (event.type == InputTypePress || event.type == InputTypeRepeat)) {
                    furi_mutex_acquire(app->mutex, FuriWaitForever);
                    if(app->history_len > 0) {
                        app->show_frame = true;
                        app->selected_index = (app->selected_index+1) % app->history_len;
                    }
                    furi_mutex_release(app->mutex);
                } else if(event.key == InputKeyOk &&
                          (event.type == InputTypeShort || event.type == InputTypeLong)) {
                    furi_mutex_acquire(app->mutex, FuriWaitForever);
                    uint8_t chosen_preset = 0;
                    uint32_t chosen_freq  = app->scan_freq;
                    if(app->show_frame && !app->signal_present && app->history_len > 0) {
                        chosen_preset = app->history[app->selected_index];
                    } else if(app->signal_present && app->leader_slot >= 0 &&
                              app->leader_slot < (int8_t)app->ncand) {
                        chosen_preset = app->cand[app->leader_slot].preset_idx;
                    }
                    furi_mutex_release(app->mutex);
                    stop_scanning(app);
                    save_trigger(app);
                    write_result_to_settings(app, chosen_freq, chosen_preset);

                    if(return_marker[0]) {
                        const char* ok_marker =
                            launched_from_garage_menu ? "gmenu:readraw" : "readraw";
                        Storage* s = furi_record_open(RECORD_STORAGE);
                        File* f = storage_file_alloc(s);
                        if(storage_file_open(f, "/ext/subghz/.focus_menu", FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
                            storage_file_write(f, ok_marker, strlen(ok_marker));
                        }
                        storage_file_close(f);
                        storage_file_free(f);
                        furi_record_close(RECORD_STORAGE);
                    }
                    app->ok_result_selected = true;
                    app->exit_requested = true;
                }
            }
        }
        view_port_update(app->view_port);
    }

    // Stop the worker (it idles the radio on exit) before we sleep the radio.
    app->scan_stop = true;
    furi_thread_join(app->scan_thread);
    furi_hal_subghz_idle();
    furi_hal_subghz_sleep();
    save_trigger(app);

    if(return_marker[0]) {
        if(!app->ok_result_selected) {
            char marker_buf[32] = {0};
            if(launched_from_garage_menu) {
                strcpy(marker_buf, "gmenu:");
                strlcat(marker_buf, return_marker, sizeof(marker_buf));
            } else {
                strncpy(marker_buf, return_marker, sizeof(marker_buf) - 1);
            }
            Storage* s = furi_record_open(RECORD_STORAGE);
            File* f = storage_file_alloc(s);
            if(storage_file_open(f, "/ext/subghz/.focus_menu", FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
                storage_file_write(f, marker_buf, strlen(marker_buf));
            }
            storage_file_close(f);
            storage_file_free(f);
            furi_record_close(RECORD_STORAGE);
        }

        Loader* loader = furi_record_open(RECORD_LOADER);
        loader_enqueue_launch(
            loader,
            "subghz",
            NULL,
            LoaderDeferredLaunchFlagNone);
        furi_record_close(RECORD_LOADER);
    }

    // scan_thread already joined above; free it and the rest.
    furi_thread_free(app->scan_thread);
    app->scan_thread = NULL;
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_message_queue_free(app->input_queue);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    subghz_setting_free(app->setting);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
