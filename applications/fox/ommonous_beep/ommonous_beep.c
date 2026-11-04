/*
 * Ommonous Beep  -  a fake "self destruct" for the Flipper Zero.
 *
 * It looks terrifying. It does nothing.
 *
 *   1. The screen goes black and holds for a beat.
 *   2. It starts beeping ~1s apart, each beep flashing the screen white and
 *      buzzing the vibro. Over ~13s the beeps accelerate (exponentially) and
 *      the pitch climbs, like a countdown winding up.
 *   3. A final ~1.8s frenzy: vibro held on, screen strobing, buzzing nonstop,
 *      as if it's about to blow.
 *   4. "Matrix" characters rain down a white screen for ~2s.
 *   5. The Flipper powers off instantly.
 *
 * Nothing is written, flashed, erased or changed. Turn it back on and it's a
 * perfectly ordinary Flipper. The whole point: an app can put on a very loud
 * show while doing absolutely nothing.
 *
 * There are no controls. Every button is a no-op by design - once it's
 * running, it runs to the end.
 */

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_speaker.h>
#include <furi_hal_vibro.h>
#include <furi_hal_power.h>
#include <furi_hal_random.h>
#include <gui/gui.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <math.h>

#define SCREEN_W 128
#define SCREEN_H 64

/* beep pitch range (Hz) for the accelerating countdown */
#define BEEP_BASE_HZ 440.0f
#define BEEP_TOP_HZ 1250.0f

/* Matrix-rain layout: a grid of character cells. */
#define MX_COL_W 8
#define MX_ROW_H 8
#define MX_COLS (SCREEN_W / MX_COL_W) /* 16 */
#define MX_ROWS (SCREEN_H / MX_ROW_H) /* 8  */
#define MX_TAIL 4 /* how many trailing chars follow each stream head */

typedef enum {
    SceneBlack, /* whole screen filled black  */
    SceneFlash, /* whole screen light (white) */
    SceneMatrix, /* white digital rain on black bg */
} Scene;

typedef struct {
    Scene scene;
    int head[MX_COLS]; /* current head row of each stream (may be negative) */
    char glyph[MX_COLS][MX_ROWS]; /* 0 = empty cell */
    FuriMutex* mutex;
} OmmonousState;

static const char MX_CHARSET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
#define MX_CHARSET_LEN (sizeof(MX_CHARSET) - 1)

static char mx_rand_glyph(void) {
    return MX_CHARSET[furi_hal_random_get() % MX_CHARSET_LEN];
}

static void mx_init(OmmonousState* st) {
    for(int c = 0; c < MX_COLS; c++) {
        /* stagger the streams so they don't all fall in lockstep */
        st->head[c] = -(int)(furi_hal_random_get() % (MX_ROWS * 2));
        for(int r = 0; r < MX_ROWS; r++) {
            st->glyph[c][r] = 0;
        }
    }
}

static void mx_step(OmmonousState* st) {
    furi_mutex_acquire(st->mutex, FuriWaitForever);
    for(int c = 0; c < MX_COLS; c++) {
        st->head[c]++;
        int hr = st->head[c];
        if(hr >= 0 && hr < MX_ROWS) {
            st->glyph[c][hr] = mx_rand_glyph(); /* fresh char at the head */
        }
        /* occasional shimmer: re-roll a random cell in this column */
        if((furi_hal_random_get() & 7) == 0) {
            st->glyph[c][furi_hal_random_get() % MX_ROWS] = mx_rand_glyph();
        }
        /* recycle the stream once its tail has left the bottom */
        if(st->head[c] - MX_TAIL > MX_ROWS) {
            st->head[c] = -(int)(furi_hal_random_get() % MX_ROWS);
            for(int r = 0; r < MX_ROWS; r++) {
                st->glyph[c][r] = 0;
            }
        }
    }
    furi_mutex_release(st->mutex);
}

static void ommonous_draw_callback(Canvas* canvas, void* ctx) {
    OmmonousState* st = ctx;
    furi_mutex_acquire(st->mutex, FuriWaitForever);

    switch(st->scene) {
    case SceneFlash:
        /* light screen - draw nothing on the cleared canvas */
        canvas_clear(canvas);
        break;

    case SceneBlack:
        canvas_clear(canvas);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 0, 0, SCREEN_W, SCREEN_H);
        break;

    case SceneMatrix:
        canvas_clear(canvas);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 0, 0, SCREEN_W, SCREEN_H); /* black background */
        canvas_set_color(canvas, ColorWhite); /* white falling text */
        canvas_set_font(canvas, FontKeyboard);
        for(int c = 0; c < MX_COLS; c++) {
            for(int t = 0; t < MX_TAIL; t++) {
                int row = st->head[c] - t;
                if(row < 0 || row >= MX_ROWS) continue;
                char ch = st->glyph[c][row];
                if(ch == 0) continue;
                canvas_draw_glyph(
                    canvas, c * MX_COL_W + 1, row * MX_ROW_H + MX_ROW_H - 1, ch);
            }
        }
        break;
    }

    furi_mutex_release(st->mutex);
}

static void ommonous_set_scene(OmmonousState* st, ViewPort* vp, Scene scene) {
    furi_mutex_acquire(st->mutex, FuriWaitForever);
    st->scene = scene;
    furi_mutex_release(st->mutex);
    view_port_update(vp);
}

int32_t ommonous_beep_app(void* p) {
    UNUSED(p);

    OmmonousState* st = malloc(sizeof(OmmonousState));
    st->scene = SceneBlack;
    st->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    mx_init(st);

    Gui* gui = furi_record_open(RECORD_GUI);
    ViewPort* vp = view_port_alloc();
    view_port_draw_callback_set(vp, ommonous_draw_callback, st);
    gui_add_view_port(gui, vp, GuiLayerFullscreen);

    NotificationApp* notifications = furi_record_open(RECORD_NOTIFICATION);
    /* keep the backlight on for the whole show */
    notification_message(notifications, &sequence_display_backlight_enforce_on);

    bool have_speaker = furi_hal_speaker_acquire(1000);

    /* ---- Scene 1: black screen, a beat of dread ---- */
    ommonous_set_scene(st, vp, SceneBlack);
    furi_delay_ms(800);

    /* ---- Scene 2: accelerating beeps, flash + vibro on each ---- */
    const uint32_t accel_ms = 13000;
    uint32_t t0 = furi_get_tick();
    for(;;) {
        uint32_t elapsed = furi_get_tick() - t0;
        if(elapsed >= accel_ms) break;
        float pr = (float)elapsed / (float)accel_ms; /* 0.0 -> 1.0 */

        /* interval shrinks exponentially: ~1000ms down to ~35ms */
        float interval = 1000.0f * powf(0.035f, pr);
        if(interval < 35.0f) interval = 35.0f;

        /* the flash/beep/buzz "on" slice of the cycle */
        float on = interval * 0.5f;
        if(on > 55.0f) on = 55.0f;
        if(on < 18.0f) on = 18.0f;
        float off = interval - on;
        if(off < 0.0f) off = 0.0f;

        /* pitch climbs as it speeds up */
        float freq = BEEP_BASE_HZ + pr * (BEEP_TOP_HZ - BEEP_BASE_HZ);

        /* ON: white flash + tone + vibro */
        ommonous_set_scene(st, vp, SceneFlash);
        if(have_speaker) furi_hal_speaker_start(freq, 1.0f);
        furi_hal_vibro_on(true);
        furi_delay_ms((uint32_t)on);

        /* OFF: back to black, silent, still */
        if(have_speaker) furi_hal_speaker_stop();
        furi_hal_vibro_on(false);
        ommonous_set_scene(st, vp, SceneBlack);
        if(off >= 1.0f) furi_delay_ms((uint32_t)off);
    }

    /* ---- Scene 3: terminal frenzy (~1.8s): constant vibro, strobe, steady top-pitch buzz ---- */
    furi_hal_vibro_on(true);
    if(have_speaker)
        furi_hal_speaker_start(BEEP_TOP_HZ, 1.0f); /* hold Scene 2's top pitch, no jump */
    uint32_t f0 = furi_get_tick();
    bool white = false;
    while(furi_get_tick() - f0 < 1800) {
        white = !white;
        ommonous_set_scene(st, vp, white ? SceneFlash : SceneBlack);
        furi_delay_ms(22);
    }
    if(have_speaker) {
        furi_hal_speaker_stop();
        furi_hal_speaker_release();
        have_speaker = false;
    }
    furi_hal_vibro_on(false);

    /* ---- Scene 4: Matrix rain on a white screen (~2s) ---- */
    mx_init(st);
    ommonous_set_scene(st, vp, SceneMatrix);
    uint32_t m0 = furi_get_tick();
    while(furi_get_tick() - m0 < 2000) {
        mx_step(st);
        view_port_update(vp);
        furi_delay_ms(25);
    }

    /* ---- Scene 5: lights out ----
     * Keep the rain on screen right up to the instant power is cut. */
    furi_delay_ms(30);
    furi_hal_power_off();

    /*
     * Only reached when a true power-off is blocked - that happens while the
     * Flipper is on USB power (the charger holds it awake). In that case we
     * tidy up and reboot instead, so the "it's gone" effect still lands.
     */
    furi_delay_ms(500);
    notification_message(notifications, &sequence_display_backlight_enforce_auto);
    furi_record_close(RECORD_NOTIFICATION);
    gui_remove_view_port(gui, vp);
    view_port_free(vp);
    furi_record_close(RECORD_GUI);
    furi_mutex_free(st->mutex);
    free(st);

    furi_hal_power_reset();
    return 0;
}
