#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/canvas.h>
#include <gui/modules/variable_item_list.h>
#include <loader/loader.h>
#include <input/input.h>
#include "subghz_raw_dual_icons.h"
#include <stdio.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/devices/preset.h>
#include <furi_hal_power.h>
#include <fox_fusion.h>
#include <dialogs/dialogs.h>
#include <storage/storage.h>
#include <lib/flipper_format/flipper_format.h>
#include <toolbox/saved_struct.h>
#include <gui/modules/text_input.h>
#include <string.h>

typedef enum {
    RawBtnRec = 0,
    RawBtnSave,
    RawBtnClear,
    RawBtnConfig,
    RawBtnCount,
} RawDualButton;

#define RAW_ENV_W 80
#define RAW_SEG_MAX 8

typedef struct {
    uint8_t selected;
    bool    recording;
    int16_t y_cur[RawBtnCount];
    int16_t y_from[RawBtnCount];
    int16_t y_to[RawBtnCount];
    int16_t x_cur[RawBtnCount];
    int16_t x_from[RawBtnCount];
    int16_t x_to[RawBtnCount];
    uint8_t anim_step;
    bool    anim_active;
    uint8_t int_freq;
    uint8_t int_mod;
    uint8_t ext_freq;
    uint8_t ext_mod;
    uint8_t int_rssi;
    uint8_t ext_rssi;
    bool    adaptive;
    bool     int_active;
    bool     ext_active;
    uint32_t int_edges;
    uint32_t ext_edges;
    uint32_t ovf;
    bool     viz_line;
    bool     frozen;
    uint8_t  seg_banked;
    uint8_t  env_int[RAW_ENV_W];
    uint8_t  env_ext[RAW_ENV_W];
} RawDualModel;

typedef struct {
    uint32_t ts;
    uint8_t  ai;
    uint8_t  ae;
} RawRssiSample;

typedef struct {
    File*     fp;
    uint32_t* buf;
    uint16_t  ind;
    uint32_t  count;
    bool      ok;
} RawDualStream;

typedef struct {
    bool    has_int;
    bool    has_ext;
    uint8_t ifreq;
    uint8_t imod;
    uint8_t efreq;
    uint8_t emod;
} RawSegCfg;

typedef struct {
    ViewDispatcher*    view_dispatcher;
    View*              view;
    VariableItemList*  cfg_list;
    FuriTimer*         anim_timer;
    Gui*               gui;
    uint8_t            save_all;
    const SubGhzDevice* dev_int;
    const SubGhzDevice* dev_ext;
    bool               ext_present;
    bool               otg_by_me;
    volatile bool      capturing;
    FuriThread*        drain_thread;
    char               prefix[16];
    bool               ti_save_mode;
    VariableItem*      prefix_item;
    TextInput*         text_input;
    uint32_t           env_acc_int;
    uint32_t           env_acc_ext;
    uint32_t           env_last_col;
    Storage*           storage;
    RawRssiSample*     rssi_trk;
    uint16_t           rssi_n;
    uint32_t           last_edge_ts;
    bool               cap_int;
    bool               cap_ext;
    uint32_t           save_freq_int;
    uint32_t           save_freq_ext;
    const char*        save_preset_int;
    const char*        save_preset_ext;
    RawDualStream      st_int;
    RawDualStream      st_ext;
    bool               have_capture;
    char               save_name[32];
    int                last_nfiles;
    char               last_savename[64];
    uint8_t            seg_count;
    RawSegCfg          seg_cfg[RAW_SEG_MAX];
} RawDualApp;

#define RAW_DUAL_VIEW_MAIN   0
#define RAW_DUAL_VIEW_CONFIG 1
#define RAW_DUAL_VIEW_PREFIX 2

typedef enum {
    RawDualEvtSaved = 1,
    RawDualEvtSaveMismatch,
    RawDualEvtSaveFail,
    RawDualEvtDoSave,
} RawDualCustomEvent;
#define RAW_CFG_PREFIX_IDX   5
#define BTN_X                94
#define BTN_W                32
#define BTN_H                13
#define RAW_ANIM_STEPS       8
#define RAW_ANIM_MS          34u
#define RAW_ENV_COL_MS       25u
#define RAW_ENV_GAIN         12u
#define RAW_RSSI_FLOOR       (-100.0f)
#define RAW_RSSI_CEIL        (-30.0f)
#define RAW_RSSI_COUNT       12
#define RAW_RSSI_DEFAULT_IDX 7
static const int16_t raw_rssi_dbm[RAW_RSSI_COUNT] =
    {-100, -95, -90, -85, -80, -75, -70, -65, -60, -55, -50, -45};
static const char* const raw_onoff[2] = {"OFF", "ON"};
#define RAW_ENV_MIN_H        1

static const char* const raw_freq_str[] = {
    "300.00", "303.87", "304.25", "310.00", "315.00", "318.00", "330.00", "390.00", "418.00",
    "433.07", "433.42", "433.92", "434.42", "434.77", "438.90", "868.35", "915.00"};
#define RAW_FREQ_COUNT   ((uint8_t)(sizeof(raw_freq_str) / sizeof(raw_freq_str[0])))
#define RAW_FREQ_DEFAULT 11
static const char* const raw_mods[] = {"AM270", "AM650", "FM238", "FM476"};
#define RAW_MOD_COUNT 4
static const FuriHalSubGhzPreset raw_mod_preset[RAW_MOD_COUNT] = {
    FuriHalSubGhzPresetOok270Async,
    FuriHalSubGhzPresetOok650Async,
    FuriHalSubGhzPreset2FSKDev238Async,
    FuriHalSubGhzPreset2FSKDev476Async};
static const char* const raw_mod_preset_name[RAW_MOD_COUNT] = {
    "FuriHalSubGhzPresetOok270Async",
    "FuriHalSubGhzPresetOok650Async",
    "FuriHalSubGhzPreset2FSKDev238Async",
    "FuriHalSubGhzPreset2FSKDev476Async"};
#define RAW_SAVE_BUF 512
#define RAW_TE_SHORT 50u
#define RAW_TMP_DIR_NS EXT_PATH("apps_data/subghz_raw_dual")
#define RAW_SUBGHZ_DIR   EXT_PATH("subghz/")
#define RAW_INTEXT_DIR   EXT_PATH("subghz/RAW-Int-Ext/")
#define RAW_INTEXT_DIR_MK EXT_PATH("subghz/RAW-Int-Ext")
#define RAW_JOIN_GAP_US  30000u
#define RAW_SEG_GAP     (80000u * 64u)
#define RAW_SEG_MINRUN  24u
#define RAW_RSSI_TRK_MAX 1024u
#define RAW_RDR_CHUNK   64u
#define RAW_EMIT_BUF    128
#define RAW_END_PAD_US   1000000
#define RAW_COMB_GAP_US   1000000u
#define RAW_COMB_REP_US   500000u
#define RAW_COMB_LOCK_LO  200u
#define RAW_COMB_LOCK_HI  1200u
#define RAW_COMB_LOCK_RUN 2u
#define RAW_BURST_MAX     32
#define RAW_KEEP_RSSI_AMP 48
static const uint32_t raw_freq_hz[] = {
    300000000, 303875000, 304250000, 310000000, 315000000, 318000000, 330000000, 390000000,
    418000000, 433075000, 433420000, 433920000, 434420000, 434775000, 438900000, 868350000,
    915000000};
#define RAW_DUAL_SETTINGS_PATH EXT_PATH("subghz/assets/last_subghz.settings")
#define RAW_DUAL_LOCAL_PATH    EXT_PATH("subghz/assets/raw_dual.settings")
#define RAW_DUAL_LOCAL_MAGIC   0x52u
#define RAW_DUAL_LOCAL_VERSION 1u

typedef struct {
    uint8_t save_all;
    char    prefix[16];
} RawDualLocal;

static const int16_t raw_ease[RAW_ANIM_STEPS] = {126, 209, 257, 279, 281, 272, 261, 256};

static void raw_dual_set_targets(RawDualModel* m) {
    for(int i = 0; i < RawBtnCount; i++) m->x_to[i] = BTN_X;
    if(m->frozen || m->seg_banked > 0) {
        m->y_to[RawBtnRec]    = 2;
        m->y_to[RawBtnSave]   = 17;
        m->y_to[RawBtnClear]  = 32;
        m->y_to[RawBtnConfig] = 47;
    } else {
        m->x_to[RawBtnSave]   = 132;
        m->x_to[RawBtnClear]  = 132;
        m->y_to[RawBtnRec]    = 17;
        m->y_to[RawBtnSave]   = 17;
        m->y_to[RawBtnClear]  = 32;
        m->y_to[RawBtnConfig] = 32;
    }
}

static void raw_dual_draw_envelope(
    Canvas* canvas,
    int32_t y,
    bool antenna,
    const char* text,
    const uint8_t* env,
    bool viz_line,
    const char* note) {

    int32_t ex0 = 1, ex1 = 90, eh = 27;
    canvas_draw_line(canvas, ex0, y, ex0, y + eh);
    canvas_draw_line(canvas, ex1, y, ex1, y + eh);
    canvas_draw_line(canvas, ex0, y + eh, ex1, y + eh);
    canvas_draw_line(canvas, ex0, y + eh - 1, ex1, y + eh - 1);
    canvas_draw_icon(
        canvas, 2, y - 1, antenna ? &I_External_ant_nb_9x11 : &I_Internal_ant_nb_9x11);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 22, y + 6, text);

    for(int32_t i = 0, tx = ex0 + 3; tx < ex1 - 1; i++, tx += 4) {
        int32_t tl = (i % 2 == 0) ? 7 : 4;
        canvas_draw_line(canvas, tx, y + 9, tx, y + 9 + tl);
    }
    int32_t base = y + eh - 4;

    if(env) {
        int32_t range = 6;
        if(viz_line) {
            int32_t px0 = -1, py0 = base;
            for(int32_t i = 0; i < RAW_ENV_W; i++) {
                int32_t x = 5 + i;
                int32_t hh = ((int32_t)env[i] * range) / 255;
                if(hh < RAW_ENV_MIN_H) hh = RAW_ENV_MIN_H;
                int32_t yt = base - hh;
                if(px0 >= 0) canvas_draw_line(canvas, px0, py0, x, yt);
                px0 = x;
                py0 = yt;
            }
        } else {
            for(int32_t i = 0; i < RAW_ENV_W; i++) {
                int32_t x = 5 + i;
                int32_t hh = ((int32_t)env[i] * range) / 255;
                if(hh < RAW_ENV_MIN_H) hh = RAW_ENV_MIN_H;
                canvas_draw_line(canvas, x, base, x, base - hh);
            }
        }
        return;
    }

    if(note) {
        canvas_draw_str(canvas, 14, y + eh - 2, note);
        return;
    }

    canvas_draw_line(canvas, 5, base, ex1 - 2, base);
}

static void
    raw_dual_draw_button(Canvas* canvas, int32_t x, int32_t y, const char* label, bool selected, bool glyph) {
    if(y >= 64 || y <= -BTN_H || x >= 128) return;
    canvas_set_color(canvas, ColorBlack);
    if(selected) {
        canvas_draw_rbox(canvas, x, y, BTN_W, BTN_H, 3);
    } else {
        canvas_draw_rframe(canvas, x, y, BTN_W, BTN_H, 3);
    }
    canvas_set_font(canvas, FontSecondary);
    if(selected) canvas_invert_color(canvas);
    if(glyph) {
        int32_t tw = (int32_t)canvas_string_width(canvas, label);
        int32_t total = 7 + 2 + tw;
        int32_t gx = x + (BTN_W - total) / 2;
        if(gx < x + 3) gx = x + 3;
        canvas_draw_icon(canvas, gx, y + 3, &I_ButtonCenter_7x7);
        canvas_draw_str(canvas, gx + 9, y + 9, label);
    } else {
        canvas_draw_str_aligned(
            canvas, x + BTN_W / 2, y + BTN_H / 2, AlignCenter, AlignCenter, label);
    }
    if(selected) canvas_invert_color(canvas);
    canvas_set_color(canvas, ColorBlack);
}

static void raw_dual_draw(Canvas* canvas, void* model) {
    RawDualModel* m = model;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    char lbl_int[20];
    char lbl_ext[20];
    snprintf(lbl_int, sizeof(lbl_int), "%s %s", raw_freq_str[m->int_freq], raw_mods[m->int_mod]);
    snprintf(lbl_ext, sizeof(lbl_ext), "%s %s", raw_freq_str[m->ext_freq], raw_mods[m->ext_mod]);
    const uint8_t* ei = NULL;
    const uint8_t* ee = NULL;
    const char* note_e = NULL;
    if(m->recording || m->frozen) {
        if(m->int_active) ei = m->env_int;
        if(m->ext_active) {
            ee = m->env_ext;
        } else {
            note_e = "not connected";
        }
    }
    raw_dual_draw_envelope(canvas, 1, true, lbl_ext, ee, m->viz_line, note_e);
    raw_dual_draw_envelope(canvas, 33, false, lbl_int, ei, m->viz_line, NULL);
    raw_dual_draw_button(
        canvas, m->x_cur[RawBtnSave], m->y_cur[RawBtnSave], "Save", m->selected == RawBtnSave, false);
    raw_dual_draw_button(
        canvas, m->x_cur[RawBtnRec], m->y_cur[RawBtnRec], m->recording ? "Stop" : "Rec",
        m->selected == RawBtnRec, true);
    raw_dual_draw_button(
        canvas, m->x_cur[RawBtnConfig], m->y_cur[RawBtnConfig], "Config",
        m->selected == RawBtnConfig, false);
    raw_dual_draw_button(
        canvas, m->x_cur[RawBtnClear], m->y_cur[RawBtnClear], "Clear", m->selected == RawBtnClear,
        false);

}

static void raw_dual_anim_cb(void* ctx) {
    RawDualApp* app = ctx;
    bool more = false;
    with_view_model(
        app->view,
        RawDualModel * m,
        {
            if(m->anim_active) {
                m->anim_step++;
                uint8_t s = m->anim_step;
                int16_t e = raw_ease[s - 1];
                for(int i = 0; i < RawBtnCount; i++) {
                    int16_t dy = m->y_to[i] - m->y_from[i];
                    m->y_cur[i] = (int16_t)(m->y_from[i] + (int16_t)((int32_t)dy * e / 256));
                    int16_t dx = m->x_to[i] - m->x_from[i];
                    m->x_cur[i] = (int16_t)(m->x_from[i] + (int16_t)((int32_t)dx * e / 256));
                }
                if(s >= RAW_ANIM_STEPS) {
                    for(int i = 0; i < RawBtnCount; i++) {
                        m->y_cur[i] = m->y_to[i];
                        m->x_cur[i] = m->x_to[i];
                    }
                    m->anim_active = false;
                } else {
                    more = true;
                }
            }
        },
        true);

    if(more) furi_timer_start(app->anim_timer, furi_ms_to_ticks(RAW_ANIM_MS));
}

static void raw_dual_cap_nop(bool level, uint32_t duration, void* ctx) {
    UNUSED(level);
    UNUSED(duration);
    UNUSED(ctx);
}

static bool raw_dual_edges_open(RawDualStream* st, Storage* storage, const char* path) {
    st->ind = 0;
    st->count = 0;
    st->ok = false;
    storage_simply_remove(storage, path);
    st->fp = storage_file_alloc(storage);
    if(!storage_file_open(st->fp, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) return false;
    st->ok = true;
    return true;
}

static void raw_dual_edges_flush(RawDualStream* st) {
    if(st->ok && st->ind > 0) {
        storage_file_write(st->fp, st->buf, (size_t)st->ind * sizeof(uint32_t));
        st->ind = 0;
    }
}

static void raw_dual_edges_push(RawDualStream* st, FoxFusionEdge e) {
    if(!st->ok || st->buf == NULL) return;
    st->buf[st->ind++] = e;
    st->count++;
    if(st->ind >= RAW_SAVE_BUF) raw_dual_edges_flush(st);
}

static void raw_dual_edges_close(RawDualStream* st) {
    if(st->fp != NULL) {
        raw_dual_edges_flush(st);
        storage_file_close(st->fp);
        storage_file_free(st->fp);
        st->fp = NULL;
    }
    st->ok = false;
}

static uint8_t raw_dual_rssi_amp(float rssi) {
    if(rssi < RAW_RSSI_FLOOR) rssi = RAW_RSSI_FLOOR;
    if(rssi > RAW_RSSI_CEIL) rssi = RAW_RSSI_CEIL;
    float norm = (rssi - RAW_RSSI_FLOOR) / (RAW_RSSI_CEIL - RAW_RSSI_FLOOR);
    return (uint8_t)(norm * 255.0f);
}

static int32_t raw_dual_drain_worker(void* ctx) {
    RawDualApp* app = ctx;
    FoxFusionEdge buf[64];
    app->env_last_col = furi_get_tick();
    float peak_int = RAW_RSSI_FLOOR, peak_ext = RAW_RSSI_FLOOR;
    while(app->capturing) {
        size_t n;
        do {
            n = fox_fusion_drain_int(buf, 64);
            for(size_t i = 0; i < n; i++) raw_dual_edges_push(&app->st_int, buf[i]);
            if(n > 0) app->last_edge_ts = FOX_FUSION_EDGE_TS(buf[n - 1]);
        } while(n == 64);
        do {
            n = fox_fusion_drain_ext(buf, 64);
            for(size_t i = 0; i < n; i++) raw_dual_edges_push(&app->st_ext, buf[i]);
            if(n > 0) app->last_edge_ts = FOX_FUSION_EDGE_TS(buf[n - 1]);
        } while(n == 64);
        if(app->dev_int != NULL) {
            float r = subghz_devices_get_rssi(app->dev_int);
            if(r > peak_int) peak_int = r;
        }
        if(app->ext_present && app->dev_ext != NULL) {
            float r = subghz_devices_get_rssi(app->dev_ext);
            if(r > peak_ext) peak_ext = r;
        }
        uint32_t now = furi_get_tick();
        if((uint32_t)(now - app->env_last_col) >= furi_ms_to_ticks(RAW_ENV_COL_MS)) {
            app->env_last_col = now;
            uint8_t ai = raw_dual_rssi_amp(peak_int);
            uint8_t ae = raw_dual_rssi_amp(peak_ext);
            if(app->rssi_trk != NULL && app->rssi_n < RAW_RSSI_TRK_MAX) {
                app->rssi_trk[app->rssi_n].ts = app->last_edge_ts;
                app->rssi_trk[app->rssi_n].ai = ai;
                app->rssi_trk[app->rssi_n].ae = ae;
                app->rssi_n++;
            }
            peak_int = RAW_RSSI_FLOOR;
            peak_ext = RAW_RSSI_FLOOR;
            uint32_t ov = fox_fusion_overflow_int() + fox_fusion_overflow_ext();
            with_view_model(
                app->view,
                RawDualModel * m,
                {
                    memmove(m->env_int, m->env_int + 1, RAW_ENV_W - 1);
                    m->env_int[RAW_ENV_W - 1] = ai;
                    memmove(m->env_ext, m->env_ext + 1, RAW_ENV_W - 1);
                    m->env_ext[RAW_ENV_W - 1] = ae;
                    m->ovf = ov;
                },
                true);
        }
        furi_delay_ms(5);
    }
    return 0;
}

static void raw_dual_no_ext_dialog(void) {
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, "External module needed", 63, 3, AlignCenter, AlignTop);
    dialog_message_set_text(
        message,
        "RAW Int+Ext needs the\ninternal radio AND an\nexternal CC1101.\nConnect one and retry.",
        0,
        17,
        AlignLeft,
        AlignTop);
    dialog_message_show(dialogs, message);
    dialog_message_free(message);
    furi_record_close(RECORD_DIALOGS);
}

static void raw_dual_lowmem_dialog(void) {
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, "Low memory", 63, 3, AlignCenter, AlignTop);
    dialog_message_set_text(
        message,
        "Not enough free RAM to\nstart capture.\nClose other apps and\nretry.",
        0,
        17,
        AlignLeft,
        AlignTop);
    dialog_message_show(dialogs, message);
    dialog_message_free(message);
    furi_record_close(RECORD_DIALOGS);
}

static void raw_dual_settings_load(
    uint8_t* int_freq,
    uint8_t* int_mod,
    uint8_t* ext_freq,
    uint8_t* ext_mod,
    bool* viz_line,
    uint8_t* int_rssi,
    uint8_t* ext_rssi,
    bool* adaptive) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    if(flipper_format_file_open_existing(ff, RAW_DUAL_SETTINGS_PATH)) {
        uint32_t v;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "DualIntFrequency", &v, 1)) {
            for(uint8_t i = 0; i < RAW_FREQ_COUNT; i++) {
                if(raw_freq_hz[i] == v) {
                    *int_freq = i;
                    break;
                }
            }
        }
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "DualIntPreset", &v, 1) && v <= 3) *int_mod = (uint8_t)v;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "DualExtFrequency", &v, 1)) {
            for(uint8_t i = 0; i < RAW_FREQ_COUNT; i++) {
                if(raw_freq_hz[i] == v) {
                    *ext_freq = i;
                    break;
                }
            }
        }
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "DualExtPreset", &v, 1) && v <= 3) *ext_mod = (uint8_t)v;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "VizMode", &v, 1)) *viz_line = (v == 1u);
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "DualIntRssi", &v, 1) && v < RAW_RSSI_COUNT)
            *int_rssi = (uint8_t)v;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "DualExtRssi", &v, 1) && v < RAW_RSSI_COUNT)
            *ext_rssi = (uint8_t)v;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "DualRssiAdaptive", &v, 1)) *adaptive = (v == 1u);
    }
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
}

static void raw_dual_settings_save(
    uint8_t int_freq, uint8_t int_mod, uint8_t ext_freq, uint8_t ext_mod, uint8_t int_rssi,
    uint8_t ext_rssi, bool adaptive) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    if(flipper_format_file_open_existing(ff, RAW_DUAL_SETTINGS_PATH)) {
        uint32_t ifr = raw_freq_hz[int_freq];
        uint32_t ipr = int_mod;
        uint32_t efr = raw_freq_hz[ext_freq];
        uint32_t epr = ext_mod;
        flipper_format_rewind(ff);
        flipper_format_insert_or_update_uint32(ff, "DualIntFrequency", &ifr, 1);
        flipper_format_rewind(ff);
        flipper_format_insert_or_update_uint32(ff, "DualIntPreset", &ipr, 1);
        flipper_format_rewind(ff);
        flipper_format_insert_or_update_uint32(ff, "DualExtFrequency", &efr, 1);
        flipper_format_rewind(ff);
        flipper_format_insert_or_update_uint32(ff, "DualExtPreset", &epr, 1);
        uint32_t ir = int_rssi, er = ext_rssi, ad = adaptive ? 1u : 0u;
        flipper_format_rewind(ff);
        flipper_format_insert_or_update_uint32(ff, "DualIntRssi", &ir, 1);
        flipper_format_rewind(ff);
        flipper_format_insert_or_update_uint32(ff, "DualExtRssi", &er, 1);
        flipper_format_rewind(ff);
        flipper_format_insert_or_update_uint32(ff, "DualRssiAdaptive", &ad, 1);
    }
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
}

static void raw_dual_seg_edge_path(char* out, size_t sz, uint8_t seg, bool ext) {
    snprintf(
        out, sz, EXT_PATH("apps_data/subghz_raw_dual/seg%u-%s.edges"), (unsigned)seg,
        ext ? "ext" : "int");
}

static void raw_dual_seg_sub_path(char* out, size_t sz, uint8_t seg, const char* kind) {
    snprintf(out, sz, EXT_PATH("apps_data/subghz_raw_dual/seg%u-%s.sub"), (unsigned)seg, kind);
}

static bool raw_dual_capture_start(RawDualApp* app) {
    uint8_t ifreq = 0, imod = 0, efreq = 0, emod = 0;
    with_view_model(
        app->view,
        RawDualModel * m,
        {
            ifreq = m->int_freq;
            imod = m->int_mod;
            efreq = m->ext_freq;
            emod = m->ext_mod;
            m->int_edges = 0;
            m->ext_edges = 0;
            m->ovf = 0;
            m->seg_banked = app->seg_count;
            m->int_active = false;
            m->ext_active = false;
            m->frozen = false;
            if(app->seg_count == 0) {
                memset(m->env_int, 0, sizeof(m->env_int));
                memset(m->env_ext, 0, sizeof(m->env_ext));
            }
        },
        false);

    app->otg_by_me = !furi_hal_power_is_otg_enabled();
    uint8_t attempts = 0;
    while(!furi_hal_power_is_otg_enabled() && attempts++ < 5) {
        furi_hal_power_enable_otg();
        furi_delay_ms(10);
    }

    app->dev_int = subghz_devices_get_by_name("cc1101_int");
    app->dev_ext = subghz_devices_get_by_name("cc1101_ext");
    app->ext_present = (app->dev_ext != NULL) && subghz_devices_is_connect(app->dev_ext);
    if(!app->ext_present) {
        if(app->otg_by_me) {
            furi_hal_power_disable_otg();
            app->otg_by_me = false;
        }
        app->dev_int = NULL;
        app->dev_ext = NULL;
        raw_dual_no_ext_dialog();
        return false;
    }

    if(!fox_fusion_arm()) {
        if(app->otg_by_me) {
            furi_hal_power_disable_otg();
            app->otg_by_me = false;
        }
        app->dev_int = NULL;
        app->dev_ext = NULL;
        app->ext_present = false;
        raw_dual_lowmem_dialog();
        return false;
    }

    app->st_int.buf = malloc(RAW_SAVE_BUF * sizeof(uint32_t));
    app->st_ext.buf = malloc(RAW_SAVE_BUF * sizeof(uint32_t));
    app->st_int.fp = NULL;
    app->st_ext.fp = NULL;
    app->rssi_trk = malloc(RAW_RSSI_TRK_MAX * sizeof(RawRssiSample));
    app->rssi_n = 0;
    app->last_edge_ts = 0;
    app->cap_int = false;
    app->cap_ext = false;

    bool int_ok = false;
    bool ext_ok = false;

    if(app->dev_int != NULL &&
       subghz_devices_is_frequency_valid(app->dev_int, raw_freq_hz[ifreq])) {
        subghz_devices_reset(app->dev_int);
        subghz_devices_idle(app->dev_int);
        subghz_devices_load_preset(app->dev_int, raw_mod_preset[imod], NULL);
        subghz_devices_set_frequency(app->dev_int, raw_freq_hz[ifreq]);
        subghz_devices_flush_rx(app->dev_int);
        subghz_devices_start_async_rx(app->dev_int, raw_dual_cap_nop, app);
        int_ok = true;
        app->cap_int = true;
        app->save_freq_int = raw_freq_hz[ifreq];
        app->save_preset_int = raw_mod_preset_name[imod];
        char seip[96];
        raw_dual_seg_edge_path(seip, sizeof(seip), app->seg_count, false);
        raw_dual_edges_open(&app->st_int, app->storage, seip);
    }

    if(app->ext_present &&
       subghz_devices_is_frequency_valid(app->dev_ext, raw_freq_hz[efreq])) {
        subghz_devices_begin(app->dev_ext);
        subghz_devices_reset(app->dev_ext);
        subghz_devices_idle(app->dev_ext);
        subghz_devices_load_preset(app->dev_ext, raw_mod_preset[emod], NULL);
        subghz_devices_set_frequency(app->dev_ext, raw_freq_hz[efreq]);
        subghz_devices_flush_rx(app->dev_ext);
        subghz_devices_start_async_rx(app->dev_ext, raw_dual_cap_nop, app);
        ext_ok = true;
        app->cap_ext = true;
        app->save_freq_ext = raw_freq_hz[efreq];
        app->save_preset_ext = raw_mod_preset_name[emod];
        char seep[96];
        raw_dual_seg_edge_path(seep, sizeof(seep), app->seg_count, true);
        raw_dual_edges_open(&app->st_ext, app->storage, seep);
    }

    with_view_model(
        app->view,
        RawDualModel * m,
        {
            m->int_active = int_ok;
            m->ext_active = ext_ok;
        },
        true);

    if(app->seg_count < RAW_SEG_MAX) {
        app->seg_cfg[app->seg_count].has_int = int_ok;
        app->seg_cfg[app->seg_count].has_ext = ext_ok;
        app->seg_cfg[app->seg_count].ifreq = ifreq;
        app->seg_cfg[app->seg_count].imod = imod;
        app->seg_cfg[app->seg_count].efreq = efreq;
        app->seg_cfg[app->seg_count].emod = emod;
    }

    app->capturing = true;
    app->drain_thread = furi_thread_alloc_ex("RawDualDrain", 2048, raw_dual_drain_worker, app);
    furi_thread_start(app->drain_thread);
    return true;
}

typedef struct {
    File*    fp;
    uint32_t chunk[RAW_RDR_CHUNK];
    size_t   nchunk;
    size_t   ichunk;
    bool     have;
    uint32_t prev;
    bool     last;
    uint32_t start;
} RawEdgeReader;

static bool raw_reader_open(RawEdgeReader* r, Storage* storage, const char* path) {
    memset(r, 0, sizeof(*r));
    r->fp = storage_file_alloc(storage);
    if(!storage_file_open(r->fp, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_free(r->fp);
        r->fp = NULL;
        return false;
    }
    return true;
}

static void raw_reader_close(RawEdgeReader* r) {
    if(r->fp != NULL) {
        storage_file_close(r->fp);
        storage_file_free(r->fp);
        r->fp = NULL;
    }
}

static bool raw_reader_next_edge(RawEdgeReader* r, FoxFusionEdge* out) {
    if(r->ichunk >= r->nchunk) {
        size_t bytes = storage_file_read(r->fp, r->chunk, sizeof(r->chunk));
        r->nchunk = bytes / sizeof(uint32_t);
        r->ichunk = 0;
        if(r->nchunk == 0) return false;
    }
    *out = r->chunk[r->ichunk++];
    return true;
}

static bool raw_reader_next_pulse(RawEdgeReader* r, int32_t* dur_us, uint32_t* abs_start) {
    FoxFusionEdge e;
    while(raw_reader_next_edge(r, &e)) {
        uint32_t ts = FOX_FUSION_EDGE_TS(e);
        bool lvl = FOX_FUSION_EDGE_LEVEL(e);
        bool emit = false;
        int32_t d = 0;
        uint32_t st = 0;
        if(r->have) {
            uint32_t dus = (ts - r->prev) / 64u;
            if(dus < 2000000u && dus > RAW_TE_SHORT && r->last != lvl) {
                d = lvl ? (int32_t)dus : -(int32_t)dus;
                st = r->start;
                r->last = lvl;
                r->start = ts;
                emit = true;
            }
        } else {
            r->last = lvl;
            r->start = ts;
            r->have = true;
        }
        r->prev = ts;
        if(emit) {
            *dur_us = d;
            *abs_start = st;
            return true;
        }
    }
    return false;
}

static bool raw_dual_scan_bounds(
    Storage* storage, const char* path, uint32_t* first_ts, uint32_t* last_ts) {
    RawEdgeReader r;
    if(!raw_reader_open(&r, storage, path)) return false;
    FoxFusionEdge e;
    bool any = false;
    uint32_t f = 0, l = 0;
    while(raw_reader_next_edge(&r, &e)) {
        uint32_t ts = FOX_FUSION_EDGE_TS(e);
        if(!any) {
            f = ts;
            any = true;
        }
        l = ts;
    }
    raw_reader_close(&r);
    if(!any) return false;
    *first_ts = f;
    *last_ts = l;
    return true;
}

typedef struct {
    FlipperFormat* ff;
    int32_t  buf[RAW_EMIT_BUF];
    uint16_t ind;
    int32_t  pend;
    bool     has_pend;
} RawEmit;

static void raw_emit_push(RawEmit* em, int32_t v) {
    if(v == 0) return;
    if(em->has_pend && ((em->pend < 0) == (v < 0))) {
        em->pend += v;
        return;
    }
    if(em->has_pend) {
        em->buf[em->ind++] = em->pend;
        if(em->ind >= RAW_EMIT_BUF) {
            flipper_format_write_int32(em->ff, "RAW_Data", em->buf, em->ind);
            em->ind = 0;
        }
    }
    em->pend = v;
    em->has_pend = true;
}

static void raw_emit_flush(RawEmit* em) {
    if(em->has_pend) {
        em->buf[em->ind++] = em->pend;
        em->has_pend = false;
    }
    if(em->ind > 0) {
        flipper_format_write_int32(em->ff, "RAW_Data", em->buf, em->ind);
        em->ind = 0;
    }
}

static void raw_dual_emit_verbatim(
    Storage* storage,
    const char* epath,
    const char* subpath,
    uint32_t freq,
    const char* preset,
    uint32_t T0,
    uint32_t first_ts,
    uint32_t T1,
    uint32_t last_ts) {
    RawEdgeReader r;
    if(!raw_reader_open(&r, storage, epath)) return;
    storage_simply_remove(storage, subpath);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    if(!flipper_format_file_open_always(ff, subpath)) {
        flipper_format_free(ff);
        raw_reader_close(&r);
        return;
    }
    flipper_format_write_header_cstr(ff, "Flipper SubGhz RAW File", 1);
    flipper_format_write_uint32(ff, "Frequency", &freq, 1);
    flipper_format_write_string_cstr(ff, "Preset", preset);
    flipper_format_write_string_cstr(ff, "Protocol", "RAW");

    RawEmit em;
    memset(&em, 0, sizeof(em));
    em.ff = ff;
    raw_emit_push(&em, -(int32_t)(((first_ts - T0) / 64u) + RAW_END_PAD_US));
    int32_t dur;
    uint32_t ab;
    while(raw_reader_next_pulse(&r, &dur, &ab)) raw_emit_push(&em, dur);
    raw_emit_push(&em, -(int32_t)(((T1 - last_ts) / 64u) + RAW_END_PAD_US));
    raw_emit_flush(&em);

    flipper_format_free(ff);
    raw_reader_close(&r);
}

typedef struct {
    uint32_t lo;
    uint32_t hi;
} RawBurst;

static int raw_dual_segment(Storage* storage, const char* path, RawBurst* out, int max) {
    RawEdgeReader r;
    if(!raw_reader_open(&r, storage, path)) return 0;
    int32_t dur;
    uint32_t ab;
    bool first = true;
    uint32_t prev_abs = 0, run_start = 0, run_len = 0;
    int nb = 0;
    while(raw_reader_next_pulse(&r, &dur, &ab)) {
        if(first) {
            run_start = ab;
            run_len = 1;
            first = false;
        } else if((ab - prev_abs) < RAW_SEG_GAP) {
            run_len++;
        } else {
            if(run_len >= RAW_SEG_MINRUN && nb < max) {
                out[nb].lo = run_start;
                out[nb].hi = prev_abs;
                nb++;
            }
            run_start = ab;
            run_len = 1;
        }
        prev_abs = ab;
    }
    if(run_len >= RAW_SEG_MINRUN && nb < max) {
        out[nb].lo = run_start;
        out[nb].hi = prev_abs;
        nb++;
    }
    raw_reader_close(&r);
    return nb;
}

static int raw_dual_union_bursts(RawBurst* a, int na, RawBurst* b, int nb, RawBurst* out, int max) {
    RawBurst all[RAW_BURST_MAX * 2];
    int cap = (int)(sizeof(all) / sizeof(all[0]));
    int n = 0;
    for(int i = 0; i < na && n < cap; i++) all[n++] = a[i];
    for(int i = 0; i < nb && n < cap; i++) all[n++] = b[i];
    for(int i = 1; i < n; i++) {
        RawBurst k = all[i];
        int j = i - 1;
        while(j >= 0 && all[j].lo > k.lo) {
            all[j + 1] = all[j];
            j--;
        }
        all[j + 1] = k;
    }
    int m = 0;
    for(int i = 0; i < n; i++) {
        if(m > 0 && all[i].lo <= out[m - 1].hi + RAW_SEG_GAP) {
            if(all[i].hi > out[m - 1].hi) out[m - 1].hi = all[i].hi;
        } else if(m < max) {
            out[m++] = all[i];
        }
    }
    return m;
}

static uint8_t raw_dual_amp_int_at(const RawRssiSample* trk, uint16_t n, uint16_t* cursor, uint32_t t) {
    if(trk == NULL || n == 0) return 255;
    uint16_t c = *cursor;
    while((uint16_t)(c + 1) < n && trk[c + 1].ts <= t) c++;
    *cursor = c;
    return trk[c].ai;
}

static int raw_dual_find_burst(const RawBurst* b, int n, uint32_t t) {
    for(int i = 0; i < n; i++) {
        uint32_t lo = (b[i].lo > RAW_SEG_GAP) ? (b[i].lo - RAW_SEG_GAP) : 0;
        uint32_t hi = b[i].hi + RAW_SEG_GAP;
        if(t >= lo && t <= hi) return i;
    }
    return -1;
}

static void raw_dual_emit_comb(
    RawDualApp* app,
    Storage* storage,
    const char* ei_path,
    const char* ee_path,
    const RawBurst* bursts,
    int nburst,
    uint32_t freq,
    const char* preset,
    const char* out_path) {
    if(nburst <= 0) return;
    uint8_t keep_amp = RAW_KEEP_RSSI_AMP;
    {
        uint8_t ri_idx = RAW_RSSI_DEFAULT_IDX;
        bool ad = true;
        with_view_model(
            app->view, RawDualModel * m, { ri_idx = m->int_rssi; ad = m->adaptive; }, false);
        if(!ad) {
            float dbm = (float)raw_rssi_dbm[ri_idx];
            float norm = (dbm - RAW_RSSI_FLOOR) / (RAW_RSSI_CEIL - RAW_RSSI_FLOOR);
            if(norm < 0.0f) norm = 0.0f;
            if(norm > 1.0f) norm = 1.0f;
            keep_amp = (uint8_t)(norm * 255.0f);
        }
    }
    RawEdgeReader ri, rx;
    if(!raw_reader_open(&ri, storage, ei_path)) return;
    if(!raw_reader_open(&rx, storage, ee_path)) {
        raw_reader_close(&ri);
        return;
    }
    storage_simply_remove(storage, out_path);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    if(!flipper_format_file_open_always(ff, out_path)) {
        flipper_format_free(ff);
        raw_reader_close(&ri);
        raw_reader_close(&rx);
        return;
    }
    flipper_format_write_header_cstr(ff, "Flipper SubGhz RAW File", 1);
    flipper_format_write_uint32(ff, "Frequency", &freq, 1);
    flipper_format_write_string_cstr(ff, "Preset", preset);
    flipper_format_write_string_cstr(ff, "Protocol", "RAW");

    RawEmit em;
    memset(&em, 0, sizeof(em));
    em.ff = ff;

    FoxFusionEdge ei, ex;
    bool hi = raw_reader_next_edge(&ri, &ei);
    bool hx = raw_reader_next_edge(&rx, &ex);
    bool int_lvl = false, ext_lvl = false, m_prev = false, m_started = false;

    uint32_t pr_prev = 0;
    bool pr_last = false;
    uint32_t pr_start = 0;

    int cur_burst = -1;
    uint32_t burst_acc_us = 0;
    bool burst_done = false;
    bool burst_locked = false;
    bool burst_started = false;
    uint8_t burst_lock_run = 0;
    bool any = false;
    uint16_t amp_cursor = 0;

    while(hi || hx) {
        uint32_t ti = hi ? FOX_FUSION_EDGE_TS(ei) : 0xFFFFFFFFu;
        uint32_t tx = hx ? FOX_FUSION_EDGE_TS(ex) : 0xFFFFFFFFu;
        uint32_t t = ti < tx ? ti : tx;
        while(hi && FOX_FUSION_EDGE_TS(ei) == t) {
            int_lvl = FOX_FUSION_EDGE_LEVEL(ei);
            hi = raw_reader_next_edge(&ri, &ei);
        }
        while(hx && FOX_FUSION_EDGE_TS(ex) == t) {
            ext_lvl = FOX_FUSION_EDGE_LEVEL(ex);
            hx = raw_reader_next_edge(&rx, &ex);
        }
        bool gate =
            raw_dual_amp_int_at(app->rssi_trk, app->rssi_n, &amp_cursor, t) >= keep_amp;
        bool M = ext_lvl || (int_lvl && gate);

        if(!m_started) {
            m_prev = M;
            m_started = true;
            pr_last = M;
            pr_start = t;
            pr_prev = t;
            continue;
        }
        if(M == m_prev) continue;
        m_prev = M;

        uint32_t dus = (t - pr_prev) / 64u;
        pr_prev = t;
        if(!(dus < 2000000u && dus > RAW_TE_SHORT)) continue;

        int32_t pdur = pr_last ? -(int32_t)dus : (int32_t)dus;
        uint32_t pstart = pr_start;
        pr_last = M;
        pr_start = t;

        int bidx = raw_dual_find_burst(bursts, nburst, pstart);
        if(bidx < 0) continue;
        if(bidx != cur_burst) {

            raw_emit_push(&em, -(int32_t)RAW_COMB_GAP_US);
            cur_burst = bidx;
            burst_acc_us = 0;
            burst_done = false;
            burst_locked = false;
            burst_started = false;
            burst_lock_run = 0;
            any = true;
            continue;
        }
        if(burst_done) continue;
        uint32_t amag = (pdur < 0) ? (uint32_t)(-pdur) : (uint32_t)pdur;
        if(!burst_locked) {

            if(amag >= RAW_COMB_LOCK_LO && amag <= RAW_COMB_LOCK_HI) {
                if(++burst_lock_run >= RAW_COMB_LOCK_RUN) burst_locked = true;
            } else {
                burst_lock_run = 0;
            }
            continue;
        }
        if(!burst_started) {
            if(pdur < 0) continue;
            burst_started = true;
        }
        raw_emit_push(&em, pdur);
        burst_acc_us += amag;
        if(burst_acc_us >= RAW_COMB_REP_US && pdur < 0) burst_done = true;
    }
    if(any) raw_emit_push(&em, -(int32_t)RAW_COMB_GAP_US);
    raw_emit_flush(&em);

    flipper_format_free(ff);
    raw_reader_close(&ri);
    raw_reader_close(&rx);
}

static int raw_dual_mod_family(const char* preset) {
    if(preset == NULL) return -1;
    if(strstr(preset, "Ook")) return 0;
    if(strstr(preset, "FSK")) return 1;
    return -1;
}

static void raw_dual_finalize_comb(
    RawDualApp* app, Storage* storage, const char* ei_path, const char* ee_path,
    const char* out_path) {
    if(!(app->cap_int && app->cap_ext)) return;
    if(app->save_freq_int != app->save_freq_ext) return;
    int raw_fam_i = raw_dual_mod_family(app->save_preset_int);
    int raw_fam_e = raw_dual_mod_family(app->save_preset_ext);
    if(raw_fam_i < 0 || raw_fam_i != raw_fam_e) return;

    static RawBurst bi[RAW_BURST_MAX], bx[RAW_BURST_MAX], bu[RAW_BURST_MAX];
    int nbi = raw_dual_segment(storage, ei_path, bi, RAW_BURST_MAX);
    int nbx = raw_dual_segment(storage, ee_path, bx, RAW_BURST_MAX);
    int nbu = raw_dual_union_bursts(bi, nbi, bx, nbx, bu, RAW_BURST_MAX);
    if(nbu <= 0) return;
    raw_dual_emit_comb(
        app, storage, ei_path, ee_path, bu, nbu, app->save_freq_int, app->save_preset_int, out_path);
}

static void raw_dual_finalize_separates(
    RawDualApp* app, Storage* storage, const char* ei_path, const char* ee_path,
    const char* oi_path, const char* oe_path, const char* oc_path) {
    uint32_t if0 = 0, il1 = 0, xf0 = 0, xl1 = 0;
    bool ig = false, xg = false;
    if(app->cap_int) ig = raw_dual_scan_bounds(storage, ei_path, &if0, &il1);
    if(app->cap_ext) xg = raw_dual_scan_bounds(storage, ee_path, &xf0, &xl1);
    FURI_LOG_I(
        "RawDual", "finalize_sep: cap_i=%d cap_e=%d ig=%d xg=%d", app->cap_int, app->cap_ext,
        ig, xg);

    uint32_t T0, T1;
    if(ig && xg) {
        T0 = if0 < xf0 ? if0 : xf0;
        T1 = il1 > xl1 ? il1 : xl1;
    } else if(ig) {
        T0 = if0;
        T1 = il1;
    } else if(xg) {
        T0 = xf0;
        T1 = xl1;
    } else {
        return;
    }

    if(ig)
        raw_dual_emit_verbatim(
            storage, ei_path, oi_path, app->save_freq_int, app->save_preset_int, T0, if0,
            T1, il1);
    if(xg)
        raw_dual_emit_verbatim(
            storage, ee_path, oe_path, app->save_freq_ext, app->save_preset_ext, T0, xf0,
            T1, xl1);

    raw_dual_finalize_comb(app, storage, ei_path, ee_path, oc_path);
}

static void raw_dual_capture_stop(RawDualApp* app) {
    if(app->dev_int != NULL) {
        subghz_devices_stop_async_rx(app->dev_int);
    }
    if(app->ext_present && app->dev_ext != NULL) {
        subghz_devices_stop_async_rx(app->dev_ext);
    }

    fox_fusion_disarm();

    app->capturing = false;
    if(app->drain_thread != NULL) {
        furi_thread_join(app->drain_thread);
        furi_thread_free(app->drain_thread);
        app->drain_thread = NULL;
    }

    fox_fusion_free();

    raw_dual_edges_close(&app->st_int);
    raw_dual_edges_close(&app->st_ext);
    if(app->st_int.buf) {
        free(app->st_int.buf);
        app->st_int.buf = NULL;
    }
    if(app->st_ext.buf) {
        free(app->st_ext.buf);
        app->st_ext.buf = NULL;
    }
    if(app->storage && app->seg_count < RAW_SEG_MAX) {
        char eip[96], eep[96], sip[96], sep[96], scp[96];
        raw_dual_seg_edge_path(eip, sizeof(eip), app->seg_count, false);
        raw_dual_seg_edge_path(eep, sizeof(eep), app->seg_count, true);
        raw_dual_seg_sub_path(sip, sizeof(sip), app->seg_count, "INT");
        raw_dual_seg_sub_path(sep, sizeof(sep), app->seg_count, "EXT");
        raw_dual_seg_sub_path(scp, sizeof(scp), app->seg_count, "COMB");
        raw_dual_finalize_separates(app, app->storage, eip, eep, sip, sep, scp);
        FURI_LOG_I(
            "RawDual", "STOP seg%u: int_edges=%lu ext_edges=%lu cap_i=%d cap_e=%d",
            (unsigned)app->seg_count, (unsigned long)app->st_int.count,
            (unsigned long)app->st_ext.count, app->cap_int, app->cap_ext);
        app->seg_count++;
    }
    if(app->rssi_trk) {
        free(app->rssi_trk);
        app->rssi_trk = NULL;
    }
    app->have_capture = true;

    if(app->dev_int != NULL) {
        subghz_devices_idle(app->dev_int);
        subghz_devices_sleep(app->dev_int);
        app->dev_int = NULL;
    }
    if(app->ext_present && app->dev_ext != NULL) {
        subghz_devices_idle(app->dev_ext);
        subghz_devices_sleep(app->dev_ext);
        subghz_devices_end(app->dev_ext);
    }
    app->dev_ext = NULL;
    app->ext_present = false;

    if(app->otg_by_me) {
        furi_hal_power_disable_otg();
        app->otg_by_me = false;
    }

    with_view_model(
        app->view, RawDualModel * m,
        {
            m->frozen = true;
            m->seg_banked = app->seg_count;
        },
        true);
}

static void raw_dual_try_start(RawDualApp* app) {
    if(!raw_dual_capture_start(app)) return;
    with_view_model(
        app->view,
        RawDualModel * m,
        {
            m->recording = true;
            m->selected = RawBtnRec;
            for(int i = 0; i < RawBtnCount; i++) {
                m->y_from[i] = m->y_cur[i];
                m->x_from[i] = m->x_cur[i];
            }
            raw_dual_set_targets(m);
            m->anim_step = 0;
            m->anim_active = true;
        },
        true);
    furi_timer_start(app->anim_timer, furi_ms_to_ticks(RAW_ANIM_MS));
}

static RawDualApp* g_raw_app = NULL;

static void raw_dual_msg(RawDualApp* app, const char* hdr, const char* body) {
    UNUSED(app);
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    DialogMessage* m = dialog_message_alloc();
    dialog_message_set_header(m, hdr, 63, 3, AlignCenter, AlignTop);
    dialog_message_set_text(m, body, 63, 32, AlignCenter, AlignCenter);
    dialog_message_set_buttons(m, NULL, "OK", NULL);
    dialog_message_show(dialogs, m);
    dialog_message_free(m);
    furi_record_close(RECORD_DIALOGS);
}

static void raw_dual_reset_capture(RawDualApp* app) {
    for(uint8_t k = 0; k < app->seg_count; k++) {
        char p[96];
        raw_dual_seg_edge_path(p, sizeof(p), k, false);
        storage_simply_remove(app->storage, p);
        raw_dual_seg_edge_path(p, sizeof(p), k, true);
        storage_simply_remove(app->storage, p);
        raw_dual_seg_sub_path(p, sizeof(p), k, "INT");
        storage_simply_remove(app->storage, p);
        raw_dual_seg_sub_path(p, sizeof(p), k, "EXT");
        storage_simply_remove(app->storage, p);
        raw_dual_seg_sub_path(p, sizeof(p), k, "COMB");
        storage_simply_remove(app->storage, p);
    }
    app->seg_count = 0;
    app->cap_int = false;
    app->cap_ext = false;
    app->have_capture = false;
    with_view_model(
        app->view,
        RawDualModel * m,
        {
            m->int_edges = 0;
            m->ext_edges = 0;
            m->ovf = 0;
            m->int_active = false;
            m->ext_active = false;
            m->viz_line = false;
            m->frozen = false;
            m->seg_banked = 0;
            memset(m->env_int, 0, sizeof(m->env_int));
            memset(m->env_ext, 0, sizeof(m->env_ext));
            for(int i = 0; i < RawBtnCount; i++) {
                m->y_from[i] = m->y_cur[i];
                m->x_from[i] = m->x_cur[i];
            }
            raw_dual_set_targets(m);
            m->anim_step = 0;
            m->anim_active = true;
            if(m->selected == RawBtnSave || m->selected == RawBtnClear)
                m->selected = RawBtnRec;
        },
        true);
    furi_timer_start(app->anim_timer, furi_ms_to_ticks(RAW_ANIM_MS));
}

static bool raw_sub_append(RawEmit* em, Storage* storage, const char* src, bool lead) {
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    if(!flipper_format_file_open_existing(ff, src)) {
        flipper_format_free(ff);
        return false;
    }
    if(lead) raw_emit_push(em, -(int32_t)RAW_JOIN_GAP_US);
    int32_t tmp[RAW_EMIT_BUF];
    uint32_t cnt = 0;
    bool any = false;
    while(flipper_format_get_value_count(ff, "RAW_Data", &cnt) && cnt > 0) {
        if(cnt > RAW_EMIT_BUF) cnt = RAW_EMIT_BUF;
        if(!flipper_format_read_int32(ff, "RAW_Data", tmp, cnt)) break;
        for(uint32_t i = 0; i < cnt; i++) raw_emit_push(em, tmp[i]);
        any = true;
    }
    flipper_format_free(ff);
    return any;
}

static bool raw_dual_write_assembled(
    Storage* storage, const char* out, uint32_t freq, const char* preset, char srcs[][96],
    int nsrc) {
    int present = 0;
    for(int i = 0; i < nsrc; i++)
        if(storage_file_exists(storage, srcs[i])) present++;
    FURI_LOG_I("RawDual", "write_assembled %s: present=%d/%d", out, present, nsrc);
    if(present == 0) return false;
    if(nsrc == 1) {
        storage_simply_remove(storage, out);
        FS_Error cp = storage_common_copy(storage, srcs[0], out);
        FURI_LOG_I("RawDual", "copy %s -> %s : err=%d", srcs[0], out, cp);
        return cp == FSE_OK;
    }
    storage_simply_remove(storage, out);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    if(!flipper_format_file_open_always(ff, out)) {
        flipper_format_free(ff);
        return false;
    }
    flipper_format_write_header_cstr(ff, "Flipper SubGhz RAW File", 1);
    flipper_format_write_uint32(ff, "Frequency", &freq, 1);
    flipper_format_write_string_cstr(ff, "Preset", preset);
    flipper_format_write_string_cstr(ff, "Protocol", "RAW");
    RawEmit em;
    memset(&em, 0, sizeof(em));
    em.ff = ff;
    bool first = true;
    bool any = false;
    for(int i = 0; i < nsrc; i++) {
        if(!storage_file_exists(storage, srcs[i])) continue;
        if(raw_sub_append(&em, storage, srcs[i], !first)) {
            any = true;
            first = false;
        }
    }
    raw_emit_flush(&em);
    flipper_format_free(ff);
    return any;
}

static int raw_dual_finalize_all(RawDualApp* app, Storage* storage, const char* name, bool* multi) {
    uint8_t n = app->seg_count;
    if(n == 0) {
        FURI_LOG_I("RawDual", "finalize_all: seg_count=0");
        return 0;
    }

    uint8_t split_start[RAW_SEG_MAX];
    int nsplit = 0;
    for(uint8_t sx = 0; sx < n; sx++) {
        bool newsplit = (sx == 0);
        if(sx > 0) {
            RawSegCfg* a = &app->seg_cfg[sx];
            RawSegCfg* b = &app->seg_cfg[sx - 1];
            if(a->has_int != b->has_int || a->has_ext != b->has_ext || a->ifreq != b->ifreq ||
               a->imod != b->imod || a->efreq != b->efreq || a->emod != b->emod)
                newsplit = true;
        }
        if(newsplit) split_start[nsplit++] = sx;
    }

    char base[64];
    {
        RawSegCfg* c = &app->seg_cfg[split_start[0]];
        const char* kind = c->has_int ? "INT" : "EXT";
        const char* lbl = c->has_int ? raw_mods[c->imod] : raw_mods[c->emod];
        char probe[200];
        for(uint32_t k = 0; k < 1000u; k++) {
            if(k == 0)
                snprintf(base, sizeof(base), "%.31s", name);
            else
                snprintf(base, sizeof(base), "%.27s (%lu)", name, (unsigned long)k);
            if(nsplit == 1)
                snprintf(probe, sizeof(probe), "%s%s-%s.sub", RAW_INTEXT_DIR, base, kind);
            else
                snprintf(
                    probe, sizeof(probe), "%s%s-%s-SPLIT1-%s.sub", RAW_INTEXT_DIR, base, kind, lbl);
            if(!storage_file_exists(storage, probe)) break;
        }
    }
    snprintf(app->last_savename, sizeof(app->last_savename), "%s", base);

    int files = 0;
    bool any_int_ext = false;
    bool nonstandard = (nsplit > 1);
    char out[200];
    static char src[RAW_SEG_MAX][96];

    for(int j = 0; j < nsplit; j++) {
        uint8_t a = split_start[j];
        uint8_t b = (j + 1 < nsplit) ? split_start[j + 1] : n;
        RawSegCfg* c = &app->seg_cfg[a];
        uint32_t ifreq = raw_freq_hz[c->ifreq], efreq = raw_freq_hz[c->efreq];
        const char* ipreset = raw_mod_preset_name[c->imod];
        const char* epreset = raw_mod_preset_name[c->emod];
        int ns;

        ns = 0;
        for(uint8_t sx = a; sx < b; sx++) raw_dual_seg_sub_path(src[ns++], 96, sx, "COMB");
        if(nsplit == 1)
            snprintf(out, sizeof(out), "%s%s.sub", RAW_INTEXT_DIR, base);
        else
            snprintf(
                out, sizeof(out), "%s%s-COMB-SPLIT%d-%s.sub", RAW_INTEXT_DIR, base, j + 1,
                raw_mods[c->imod]);
        bool comb_ok = raw_dual_write_assembled(storage, out, ifreq, ipreset, src, ns);
        if(comb_ok) files++;
        if(c->has_int && c->has_ext && !comb_ok) nonstandard = true;

        bool want_sep = app->save_all || !comb_ok;

        if(want_sep && c->has_int) {
            ns = 0;
            for(uint8_t sx = a; sx < b; sx++) raw_dual_seg_sub_path(src[ns++], 96, sx, "INT");
            if(nsplit == 1)
                snprintf(out, sizeof(out), "%s%s-INT.sub", RAW_INTEXT_DIR, base);
            else
                snprintf(
                    out, sizeof(out), "%s%s-INT-SPLIT%d-%s.sub", RAW_INTEXT_DIR, base, j + 1,
                    raw_mods[c->imod]);
            if(raw_dual_write_assembled(storage, out, ifreq, ipreset, src, ns)) {
                files++;
                any_int_ext = true;
            }
        }

        if(want_sep && c->has_ext) {
            ns = 0;
            for(uint8_t sx = a; sx < b; sx++) raw_dual_seg_sub_path(src[ns++], 96, sx, "EXT");
            if(nsplit == 1)
                snprintf(out, sizeof(out), "%s%s-EXT.sub", RAW_INTEXT_DIR, base);
            else
                snprintf(
                    out, sizeof(out), "%s%s-EXT-SPLIT%d-%s.sub", RAW_INTEXT_DIR, base, j + 1,
                    raw_mods[c->emod]);
            if(raw_dual_write_assembled(storage, out, efreq, epreset, src, ns)) {
                files++;
                any_int_ext = true;
            }
        }
    }

    if(nsplit > 1 && any_int_ext) {
        RawSegCfg* c0 = &app->seg_cfg[0];
        int ns = 0;
        for(uint8_t sx = 0; sx < n; sx++)
            if(app->seg_cfg[sx].has_int) raw_dual_seg_sub_path(src[ns++], 96, sx, "INT");
        if(ns > 0) {
            snprintf(out, sizeof(out), "%s%s-INT-JOIN-ALL-UNPLAYABLE.sub", RAW_INTEXT_DIR, base);
            if(raw_dual_write_assembled(
                   storage, out, raw_freq_hz[c0->ifreq], raw_mod_preset_name[c0->imod], src, ns))
                files++;
        }
        ns = 0;
        for(uint8_t sx = 0; sx < n; sx++)
            if(app->seg_cfg[sx].has_ext) raw_dual_seg_sub_path(src[ns++], 96, sx, "EXT");
        if(ns > 0) {
            snprintf(out, sizeof(out), "%s%s-EXT-JOIN-ALL-UNPLAYABLE.sub", RAW_INTEXT_DIR, base);
            if(raw_dual_write_assembled(
                   storage, out, raw_freq_hz[c0->efreq], raw_mod_preset_name[c0->emod], src, ns))
                files++;
        }
    }

    if(multi) *multi = nonstandard;
    return files;
}

static void raw_dual_save_do(RawDualApp* app) {
    const char* nm = app->save_name[0] ? app->save_name : "RawDual";
    bool mkok = storage_simply_mkdir(app->storage, RAW_INTEXT_DIR_MK);
    FURI_LOG_I("RawDual", "mkdir %s -> %d", RAW_INTEXT_DIR_MK, mkok);
    bool multi = false;
    int nfiles = raw_dual_finalize_all(app, app->storage, nm, &multi);
    app->last_nfiles = nfiles;
    raw_dual_reset_capture(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, RAW_DUAL_VIEW_MAIN);
    view_dispatcher_send_custom_event(
        app->view_dispatcher,
        (nfiles == 0) ? RawDualEvtSaveFail : multi ? RawDualEvtSaveMismatch : RawDualEvtSaved);
}

static void raw_dual_save_done(void* context) {
    RawDualApp* app = context;
    app->ti_save_mode = false;
    view_dispatcher_show_loading(app->view_dispatcher);
    view_dispatcher_send_custom_event(app->view_dispatcher, RawDualEvtDoSave);
}

static bool raw_dual_custom_event(void* context, uint32_t event) {
    RawDualApp* app = context;
    switch(event) {
    case RawDualEvtSaved: {
        char hdr[24];
        if(app->last_nfiles > 1)
            snprintf(hdr, sizeof(hdr), "Saved %d Files!", app->last_nfiles);
        else
            snprintf(hdr, sizeof(hdr), "Saved");
        raw_dual_msg(
            app, hdr,
            app->last_savename[0] ? app->last_savename :
                                   (app->save_name[0] ? app->save_name : "RawDual"));
        return true;
    }
    case RawDualEvtSaveMismatch: {
        char hdr[24], body[96];
        snprintf(
            hdr, sizeof(hdr), "Saved %d Files!", app->last_nfiles > 1 ? app->last_nfiles : 2);
        snprintf(
            body, sizeof(body), "Mixed freq/mod.\nPrefix: %s-",
            app->last_savename[0] ? app->last_savename :
                                   (app->save_name[0] ? app->save_name : "RawDual"));
        raw_dual_msg(app, hdr, body);
        return true;
    }
    case RawDualEvtSaveFail:
        raw_dual_msg(app, "Save failed", "Nothing was written.");
        return true;
    case RawDualEvtDoSave:
        raw_dual_save_do(app);
        return true;
    default:
        return false;
    }
}

static bool raw_dual_input(InputEvent* event, void* context) {
    RawDualApp* app = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;
    bool consumed = false;
    bool open_config = false;
    bool start_anim = false;
    bool stop_capture = false;
    bool req_start = false;
    bool req_save = false;
    bool req_clear = false;
    with_view_model(
        app->view,
        RawDualModel * m,
        {
            switch(event->key) {
            case InputKeyUp:
                if(!(m->frozen || m->seg_banked > 0))
                    m->selected = RawBtnRec;
                else if(m->selected > RawBtnRec)
                    m->selected--;
                consumed = true;
                break;
            case InputKeyDown:
                if(!(m->frozen || m->seg_banked > 0))
                    m->selected = RawBtnConfig;
                else if(m->selected < RawBtnConfig)
                    m->selected++;
                consumed = true;
                break;
            case InputKeyOk:
                if(m->selected == RawBtnRec) {
                    if(m->recording) {
                        m->recording = false;
                        m->frozen = true;
                        for(int i = 0; i < RawBtnCount; i++) {
                m->y_from[i] = m->y_cur[i];
                m->x_from[i] = m->x_cur[i];
            }
                        raw_dual_set_targets(m);
                        m->anim_step = 0;
                        m->anim_active = true;
                        start_anim = true;
                        stop_capture = true;
                    } else {
                        req_start = true;
                    }
                } else if(m->selected == RawBtnConfig) {
                    open_config = true;
                } else if(m->selected == RawBtnSave) {
                    req_save = true;
                } else if(m->selected == RawBtnClear) {
                    req_clear = true;
                }
                consumed = true;
                break;
            default:
                break;
            }
        },
        true);
    if(start_anim) furi_timer_start(app->anim_timer, furi_ms_to_ticks(RAW_ANIM_MS));
    if(stop_capture) raw_dual_capture_stop(app);
    if(req_start) {
        if(app->seg_count >= RAW_SEG_MAX) {
            raw_dual_msg(app, "Max segments", "Save or Clear first.");
        } else {
            raw_dual_try_start(app);
        }
    }
    if(open_config) {
        view_dispatcher_switch_to_view(app->view_dispatcher, RAW_DUAL_VIEW_CONFIG);
    }
    if(req_save) {
        if(app->seg_count == 0) {
            raw_dual_msg(app, "Nothing to save", "Record a capture first.");
        } else {
            app->ti_save_mode = true;
            text_input_reset(app->text_input);
            text_input_set_header_text(app->text_input, "Save as");
            snprintf(
                app->save_name, sizeof(app->save_name), "%s",
                app->prefix[0] ? app->prefix : "RawDual");
            text_input_set_result_callback(
                app->text_input, raw_dual_save_done, app, app->save_name, sizeof(app->save_name),
                false);
            view_dispatcher_switch_to_view(app->view_dispatcher, RAW_DUAL_VIEW_PREFIX);
        }
    }
    if(req_clear) {
        raw_dual_reset_capture(app);
        raw_dual_msg(app, "Cleared", "Capture buffer discarded.");
    }
    return consumed;
}

static bool raw_dual_nav(void* context) {
    RawDualApp* app = context;

    Loader* loader = furi_record_open(RECORD_LOADER);
    loader_enqueue_launch(loader, "subghz", NULL, LoaderDeferredLaunchFlagNone);
    furi_record_close(RECORD_LOADER);
    view_dispatcher_stop(app->view_dispatcher);
    return true;
}

static uint32_t raw_dual_cfg_prev(void* context) {
    UNUSED(context);
    return RAW_DUAL_VIEW_MAIN;
}

static void raw_dual_cfg_rssi_text(VariableItem* item, uint8_t idx) {
    if(idx >= RAW_RSSI_COUNT) idx = RAW_RSSI_COUNT - 1;
    char buf[12];
    snprintf(buf, sizeof(buf), "%d dB", (int)raw_rssi_dbm[idx]);
    variable_item_set_current_value_text(item, buf);
}

static void raw_dual_cfg_int_rssi_changed(VariableItem* item) {
    RawDualApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    raw_dual_cfg_rssi_text(item, idx);
    with_view_model(app->view, RawDualModel * m, { m->int_rssi = idx; }, false);
}

static void raw_dual_cfg_ext_rssi_changed(VariableItem* item) {
    RawDualApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    raw_dual_cfg_rssi_text(item, idx);
    with_view_model(app->view, RawDualModel * m, { m->ext_rssi = idx; }, false);
}

static void raw_dual_cfg_adaptive_changed(VariableItem* item) {
    RawDualApp* app = variable_item_get_context(item);
    uint8_t on = variable_item_get_current_value_index(item) ? 1 : 0;
    variable_item_set_current_value_text(item, raw_onoff[on]);
    with_view_model(app->view, RawDualModel * m, { m->adaptive = (on == 1); }, false);
}

static void raw_dual_cfg_int_freq_changed(VariableItem* item) {
    RawDualApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, raw_freq_str[idx]);
    with_view_model(app->view, RawDualModel * m, { m->int_freq = idx; }, false);
}

static void raw_dual_cfg_int_mod_changed(VariableItem* item) {
    RawDualApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, raw_mods[idx]);
    with_view_model(app->view, RawDualModel * m, { m->int_mod = idx; }, false);
}

static void raw_dual_cfg_ext_freq_changed(VariableItem* item) {
    RawDualApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, raw_freq_str[idx]);
    with_view_model(app->view, RawDualModel * m, { m->ext_freq = idx; }, false);
}

static void raw_dual_cfg_ext_mod_changed(VariableItem* item) {
    RawDualApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, raw_mods[idx]);
    with_view_model(app->view, RawDualModel * m, { m->ext_mod = idx; }, false);
}

static void raw_dual_cfg_save_changed(VariableItem* item) {
    RawDualApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, idx ? "All" : "Combine");
    app->save_all = idx;
}

static void raw_dual_local_load(RawDualApp* app) {
    RawDualLocal local;
    memset(&local, 0, sizeof(local));
    if(saved_struct_load(
           RAW_DUAL_LOCAL_PATH, &local, sizeof(local), RAW_DUAL_LOCAL_MAGIC, RAW_DUAL_LOCAL_VERSION)) {
        app->save_all = local.save_all ? 1 : 0;
        local.prefix[sizeof(local.prefix) - 1] = '\0';
        strncpy(app->prefix, local.prefix, sizeof(app->prefix) - 1);
        app->prefix[sizeof(app->prefix) - 1] = '\0';
    }
}

static void raw_dual_local_save(RawDualApp* app) {
    RawDualLocal local;
    memset(&local, 0, sizeof(local));
    local.save_all = app->save_all;
    strncpy(local.prefix, app->prefix, sizeof(local.prefix) - 1);
    saved_struct_save(
        RAW_DUAL_LOCAL_PATH, &local, sizeof(local), RAW_DUAL_LOCAL_MAGIC, RAW_DUAL_LOCAL_VERSION);
}

static void raw_dual_prefix_done(void* context) {
    RawDualApp* app = context;
    app->prefix[sizeof(app->prefix) - 1] = '\0';
    variable_item_set_current_value_text(app->prefix_item, app->prefix[0] ? app->prefix : "(none)");
    raw_dual_local_save(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, RAW_DUAL_VIEW_CONFIG);
}

static uint32_t raw_dual_prefix_prev(void* context) {
    UNUSED(context);
    return (g_raw_app && g_raw_app->ti_save_mode) ? RAW_DUAL_VIEW_MAIN : RAW_DUAL_VIEW_CONFIG;
}

static void raw_dual_cfg_enter(void* context, uint32_t index) {
    RawDualApp* app = context;
    if(index == RAW_CFG_PREFIX_IDX) {
        app->ti_save_mode = false;
        text_input_reset(app->text_input);
        text_input_set_header_text(app->text_input, "RAW filename prefix");
        text_input_set_result_callback(
            app->text_input, raw_dual_prefix_done, app, app->prefix, sizeof(app->prefix), false);
        view_dispatcher_switch_to_view(app->view_dispatcher, RAW_DUAL_VIEW_PREFIX);
    }
}

int32_t subghz_raw_dual_app(void* p) {
    UNUSED(p);
    RawDualApp* app = malloc(sizeof(RawDualApp));
    app->save_all = 0;
    app->seg_count = 0;
    app->save_name[0] = '\0';
    app->ti_save_mode = false;
    g_raw_app = app;
    app->dev_int = NULL;
    app->dev_ext = NULL;
    app->ext_present = false;
    app->otg_by_me = false;
    app->capturing = false;
    app->drain_thread = NULL;
    app->storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(app->storage, RAW_TMP_DIR_NS);
    app->rssi_trk = NULL;
    app->rssi_n = 0;
    app->st_int.fp = NULL;
    app->st_int.buf = NULL;
    app->st_ext.fp = NULL;
    app->st_ext.buf = NULL;
    app->have_capture = false;
    app->prefix[0] = '\0';

    subghz_devices_init();

    uint8_t s_int_freq = RAW_FREQ_DEFAULT, s_int_mod = 1, s_ext_freq = RAW_FREQ_DEFAULT,
            s_ext_mod = 3;
    bool s_viz_line = false;
    uint8_t s_int_rssi = RAW_RSSI_DEFAULT_IDX, s_ext_rssi = RAW_RSSI_DEFAULT_IDX;
    bool s_adaptive = true;
    raw_dual_settings_load(
        &s_int_freq, &s_int_mod, &s_ext_freq, &s_ext_mod, &s_viz_line, &s_int_rssi, &s_ext_rssi,
        &s_adaptive);
    raw_dual_local_load(app);

    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, raw_dual_custom_event);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, raw_dual_nav);

    app->view = view_alloc();
    view_set_context(app->view, app);
    view_set_draw_callback(app->view, raw_dual_draw);
    view_set_input_callback(app->view, raw_dual_input);
    view_allocate_model(app->view, ViewModelTypeLocking, sizeof(RawDualModel));
    with_view_model(
        app->view, RawDualModel * m, {
            m->selected = RawBtnRec;
            m->recording = false;
            m->anim_active = false;
            m->anim_step = 0;
            m->int_freq = s_int_freq;
            m->int_mod = s_int_mod;
            m->ext_freq = s_ext_freq;
            m->ext_mod = s_ext_mod;
            m->int_active = false;
            m->ext_active = false;
            m->int_edges = 0;
            m->ext_edges = 0;
            m->ovf = 0;
            m->viz_line = s_viz_line;
            m->int_rssi = s_int_rssi;
            m->ext_rssi = s_ext_rssi;
            m->adaptive = s_adaptive;
            m->frozen = false;
            memset(m->env_int, 0, sizeof(m->env_int));
            memset(m->env_ext, 0, sizeof(m->env_ext));
            raw_dual_set_targets(m);
            for(int i = 0; i < RawBtnCount; i++) {
                m->y_cur[i] = m->y_to[i];
                m->y_from[i] = m->y_to[i];
                m->x_cur[i] = m->x_to[i];
                m->x_from[i] = m->x_to[i];
            }
        },
        true);
    view_dispatcher_add_view(app->view_dispatcher, RAW_DUAL_VIEW_MAIN, app->view);

    app->anim_timer = furi_timer_alloc(raw_dual_anim_cb, FuriTimerTypeOnce, app);

    app->cfg_list = variable_item_list_alloc();
    VariableItem* item;
    item = variable_item_list_add(
        app->cfg_list, "Int Freq", RAW_FREQ_COUNT, raw_dual_cfg_int_freq_changed, app);
    variable_item_set_current_value_index(item, s_int_freq);
    variable_item_set_current_value_text(item, raw_freq_str[s_int_freq]);
    item = variable_item_list_add(
        app->cfg_list, "Int Mod", RAW_MOD_COUNT, raw_dual_cfg_int_mod_changed, app);
    variable_item_set_current_value_index(item, s_int_mod);
    variable_item_set_current_value_text(item, raw_mods[s_int_mod]);
    item = variable_item_list_add(
        app->cfg_list, "Int RSSI", RAW_RSSI_COUNT, raw_dual_cfg_int_rssi_changed, app);
    variable_item_set_current_value_index(item, s_int_rssi);
    raw_dual_cfg_rssi_text(item, s_int_rssi);
    item = variable_item_list_add(
        app->cfg_list, "Ext Freq", RAW_FREQ_COUNT, raw_dual_cfg_ext_freq_changed, app);
    variable_item_set_current_value_index(item, s_ext_freq);
    variable_item_set_current_value_text(item, raw_freq_str[s_ext_freq]);
    item = variable_item_list_add(
        app->cfg_list, "Ext Mod", RAW_MOD_COUNT, raw_dual_cfg_ext_mod_changed, app);
    variable_item_set_current_value_index(item, s_ext_mod);
    variable_item_set_current_value_text(item, raw_mods[s_ext_mod]);
    item = variable_item_list_add(
        app->cfg_list, "Ext RSSI", RAW_RSSI_COUNT, raw_dual_cfg_ext_rssi_changed, app);
    variable_item_set_current_value_index(item, s_ext_rssi);
    raw_dual_cfg_rssi_text(item, s_ext_rssi);
    item = variable_item_list_add(
        app->cfg_list, "Adaptive RSSI", 2, raw_dual_cfg_adaptive_changed, app);
    variable_item_set_current_value_index(item, s_adaptive ? 1 : 0);
    variable_item_set_current_value_text(item, raw_onoff[s_adaptive ? 1 : 0]);
    item = variable_item_list_add(app->cfg_list, "Save", 2, raw_dual_cfg_save_changed, app);
    variable_item_set_current_value_index(item, app->save_all);
    variable_item_set_current_value_text(item, app->save_all ? "All" : "Combine");
    item = variable_item_list_add(app->cfg_list, "Prefix", 1, NULL, app);
    variable_item_set_current_value_text(item, app->prefix[0] ? app->prefix : "(none)");
    app->prefix_item = item;
    variable_item_list_set_enter_callback(app->cfg_list, raw_dual_cfg_enter, app);
    view_set_previous_callback(variable_item_list_get_view(app->cfg_list), raw_dual_cfg_prev);
    view_dispatcher_add_view(
        app->view_dispatcher, RAW_DUAL_VIEW_CONFIG, variable_item_list_get_view(app->cfg_list));

    app->text_input = text_input_alloc();
    view_set_previous_callback(text_input_get_view(app->text_input), raw_dual_prefix_prev);
    view_dispatcher_add_view(
        app->view_dispatcher, RAW_DUAL_VIEW_PREFIX, text_input_get_view(app->text_input));

    view_dispatcher_switch_to_view(app->view_dispatcher, RAW_DUAL_VIEW_MAIN);
    view_dispatcher_run(app->view_dispatcher);

    if(app->capturing) raw_dual_capture_stop(app);

    {
        uint8_t fi = RAW_FREQ_DEFAULT, fm = 1, ei = RAW_FREQ_DEFAULT, em = 3;
        uint8_t ri = RAW_RSSI_DEFAULT_IDX, re = RAW_RSSI_DEFAULT_IDX;
        bool ad = true;
        with_view_model(
            app->view, RawDualModel * m, {
                fi = m->int_freq;
                fm = m->int_mod;
                ei = m->ext_freq;
                em = m->ext_mod;
                ri = m->int_rssi;
                re = m->ext_rssi;
                ad = m->adaptive;
            }, false);
        raw_dual_settings_save(fi, fm, ei, em, ri, re, ad);
    }
    raw_dual_local_save(app);

    furi_timer_stop(app->anim_timer);
    furi_timer_free(app->anim_timer);
    view_dispatcher_remove_view(app->view_dispatcher, RAW_DUAL_VIEW_PREFIX);
    view_dispatcher_remove_view(app->view_dispatcher, RAW_DUAL_VIEW_CONFIG);
    view_dispatcher_remove_view(app->view_dispatcher, RAW_DUAL_VIEW_MAIN);
    variable_item_list_free(app->cfg_list);
    text_input_free(app->text_input);
    view_free(app->view);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);
    subghz_devices_deinit();
    if(app->storage) {
        storage_simply_remove_recursive(app->storage, RAW_TMP_DIR_NS);
        furi_record_close(RECORD_STORAGE);
        app->storage = NULL;
    }
    free(app);
    return 0;
}
