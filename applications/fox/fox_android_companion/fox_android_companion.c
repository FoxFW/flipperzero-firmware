// Fox Android Companion — headless IO daemon driven by the FoxFW Android app over BLE/USB.
//
// Launched on demand (the app AppStarts it over RPC with the data dir as its arg; desktop can also
// flag-launch it). It services commands through files so the app never has to launch a FAP per
// command:
//
//   /ext/apps_data/fox_companion/cmd.txt   app writes "<CHAN> <payload>\n<captureMs>\n"
//   /ext/apps_data/fox_companion/out.txt   daemon writes the reply here
//   /ext/apps_data/fox_companion/stop      app touches this to make the daemon exit
//
// Channels (this slice):
//   CLI <command>   run a Flipper CLI command through a real cli_shell on a pipe, capture its output
//   ESP32 <line>    forward a line to the ESP32 on the GPIO UART (pin 13=TX, 14=RX, 115200 8N1),
//                   capture the board's reply for captureMs
//
// ONE-SHOT: launched with the data dir as its arg, it reads ONE command from cmd.txt, runs it,
// writes out.txt, and EXITS immediately — so the Flipper's loader is free again between commands and
// the device never appears "frozen". The app re-launches it per command. If the loader is busy
// (another app open) the app's appStart simply fails, nothing is left running, and the app banks the
// command to replay once the user is back on the Flipper desktop. Opened by hand (no launch arg) it
// shows a short "working" splash and exits — headless, meant to be driven by the app.

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>
#include <expansion/expansion.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <gui/elements.h>
#include <input/input.h>
#include <storage/storage.h>
#include <toolbox/pipe.h>
#include <toolbox/cli/cli_registry.h>
#include <toolbox/cli/cli_command.h>
#include <toolbox/cli/shell/cli_shell.h>
#include <cli/cli.h> // RECORD_CLI (shared main CLI registry)
#include "fox_android_companion_icons.h" // generated from icons/ (fap_icon_assets) -> I_fox_right_32x32
#include "fox_progress.h"
#include "fox_watchdog.h"
#include "fox_ch_subghz.h"
#include "fox_ch_nfc.h"
#include "fox_ch_settings.h"
#include "fox_ch_update.h"
#include <stdlib.h>
#include <string.h>

#define TAG "FoxCompanion"

#define FOX_DIR  EXT_PATH("apps_data/fox_companion")
#define FOX_CMD  EXT_PATH("apps_data/fox_companion/cmd.txt")
#define FOX_OUT  EXT_PATH("apps_data/fox_companion/out.txt")
#define FOX_STOP EXT_PATH("apps_data/fox_companion/stop")
#define FOX_PAINT_BIN  EXT_PATH("apps_data/fox_companion/paint.bin")  // 128x64 1bpp XBM the app live-pushes
#define FOX_PAINT_STOP EXT_PATH("apps_data/fox_companion/paint.stop") // app touches this to leave paint mode
#define FOX_PAINT_GONE EXT_PATH("apps_data/fox_companion/paint.gone") // FAP touches on exit -> app knows it closed
#define FOX_IOSTATUS EXT_PATH("apps_data/fox_companion/io_status.txt")

#define OUT_CAP 60000u

// ESP32 GPIO-UART relay settings (matches the BFFB v2 wiring).
#define UART_BAUD    115200u
#define RX_STREAM_SZ 8192

// Full Flipper CLI: builtin + external cli_ plugins (device_info, storage, ps, free, loader, ...).
static const CliCommandExternalConfig FOX_CLI_EXT = {
    .search_directory = "/ext/apps_data/cli/plugins",
    .fal_prefix = "cli_",
    .appid = "cli",
};

// ---------------------------------------------------------------------------
// manual-open splash (only when opened by hand, i.e. no launch arg)
// ---------------------------------------------------------------------------

typedef struct {
    int phase; // 0 = loading wheel, 1 = "APP is Working!"
    uint8_t frame;
} Splash;

// Same 8-dot fade-trail wheel the firmware's Loading module draws, centred on the screen.
static const int8_t FOX_SPIN_DX[8] = {0, 8, 11, 8, 0, -8, -11, -8};
static const int8_t FOX_SPIN_DY[8] = {-11, -8, 0, 8, 11, 8, 0, -8};

static void fox_splash_draw(Canvas* canvas, void* ctx) {
    Splash* s = ctx;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    if(s->phase == 0) {
        const uint8_t frame = s->frame;
        for(uint8_t i = 0; i < 8; i++) {
            uint8_t x = (uint8_t)(64 + FOX_SPIN_DX[i]);
            uint8_t y = (uint8_t)(32 + FOX_SPIN_DY[i]);
            uint8_t age = (uint8_t)((8u + frame - i) % 8u);
            if(age == 0)
                canvas_draw_disc(canvas, x, y, 3);
            else if(age == 1)
                canvas_draw_disc(canvas, x, y, 2);
            else if(age == 2)
                canvas_draw_disc(canvas, x, y, 1);
            else
                canvas_draw_dot(canvas, x, y);
        }
    } else {
        canvas_set_font(canvas, FontPrimary); // bold
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "App is Running!");
    }
}

static void fox_manual_splash(void) {
    Gui* gui = furi_record_open(RECORD_GUI);
    ViewPort* vp = view_port_alloc();
    Splash s = {.phase = 0, .frame = 0};
    view_port_draw_callback_set(vp, fox_splash_draw, &s);
    gui_add_view_port(gui, vp, GuiLayerFullscreen);

    // Loading wheel for ~2s (50 ms per frame, like the firmware Loading view).
    for(uint32_t t = 0; t < 2000u; t += 50u) {
        s.frame = (uint8_t)((s.frame + 1u) % 8u);
        view_port_update(vp);
        furi_delay_ms(50);
    }
    // "APP is Working!" centred (v/h) and bold for ~2s.
    s.phase = 1;
    view_port_update(vp);
    furi_delay_ms(2000);

    gui_remove_view_port(gui, vp);
    view_port_free(vp);
    furi_record_close(RECORD_GUI);
}

// ---------------------------------------------------------------------------
// busy screen + cancel
// ---------------------------------------------------------------------------
//
// The daemon is invisible while it services a command — the Flipper just sits on the desktop/loader.
// That's exactly what we want for the common fast path, but a long op (an ESP32 capture, a slow CLI
// dump) can hold the device for seconds with nothing on screen, so a user with no feedback will
// reasonably think it has hung. We reveal a screen progressively, timed by how long the op *actually*
// runs — not by its requested capture window:
//   < 3s : nothing appears at all (fast commands stay fully invisible)
//   3-4s : a right-facing fox + "Operation in progress!" message, no buttons yet
//   > 4s : the OK/Back buttons appear; short-press BACK then cancels
// When the op ends the whole screen is torn down and the desktop returns instantly. The main thread
// blocks in the capture loops, so it can't animate the screen itself — a tiny watcher thread stages
// the reveal while the op runs. g_cancel is written from the GUI input thread and polled by the
// capture loops, hence volatile. (A long BACK hold still force-closes via the loader as always.)
static volatile bool g_cancel = false;

typedef struct {
    Gui* gui;
    ViewPort* vp;
    FuriThread* thread;
    volatile int phase;  // 0 = hidden, 1 = message only, 2 = message + buttons
    volatile bool added; // viewport has been added to the GUI (the watcher owns the add)
    volatile bool done;  // the op finished — watcher should stop waiting
} BusyUi;

static void fox_busy_draw(Canvas* canvas, void* ctx) {
    BusyUi* b = ctx;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    // Right-facing fox on the left, looking across at the message.
    canvas_draw_icon(canvas, 4, 9, &I_fox_right_32x32);
    canvas_set_font(canvas, FontPrimary); // bold
    canvas_draw_str(canvas, 42, 18, "Android App");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 42, 30, "Operation");
    canvas_draw_str(canvas, 42, 40, "in progress!");
    if(b->phase >= 2) {
        // OK = keep waiting, Back = cancel now. Cancel is filled so it reads as the live action.
        canvas_draw_rframe(canvas, 4, 49, 54, 13, 3);
        canvas_draw_str_aligned(canvas, 31, 55, AlignCenter, AlignCenter, "OK: Wait");
        canvas_draw_rbox(canvas, 70, 49, 54, 13, 3);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str_aligned(canvas, 97, 55, AlignCenter, AlignCenter, "Back: Cancel");
    }
}

static void fox_busy_input(InputEvent* event, void* ctx) {
    BusyUi* b = ctx;
    // Only actionable once the buttons are actually on screen (> 2s in).
    if(b->phase >= 2 && event->type == InputTypeShort && event->key == InputKeyBack) {
        g_cancel = true;
    }
}

static int32_t fox_busy_thread(void* ctx) {
    BusyUi* b = ctx;
    // Stage 1: wait ~3s. If the op already finished, show nothing at all.
    for(uint32_t t = 0; t < 3000u && !b->done; t += 20u) furi_delay_ms(20);
    if(b->done) return 0;
    b->phase = 1;
    gui_add_view_port(b->gui, b->vp, GuiLayerFullscreen);
    b->added = true;
    view_port_update(b->vp);
    // Stage 2: another ~1s (so ~4s total), then reveal the buttons.
    for(uint32_t t = 0; t < 1000u && !b->done; t += 20u) furi_delay_ms(20);
    if(b->done) return 0;
    b->phase = 2;
    view_port_update(b->vp);
    return 0;
}

static void fox_busy_start(BusyUi* b) {
    g_cancel = false;
    b->gui = furi_record_open(RECORD_GUI);
    b->vp = view_port_alloc();
    b->phase = 0;
    b->added = false;
    b->done = false;
    view_port_draw_callback_set(b->vp, fox_busy_draw, b);
    view_port_input_callback_set(b->vp, fox_busy_input, b);
    b->thread = furi_thread_alloc_ex("FoxBusy", 2048, fox_busy_thread, b);
    furi_thread_start(b->thread);
}

static void fox_busy_stop(BusyUi* b) {
    b->done = true;
    furi_thread_join(b->thread);
    furi_thread_free(b->thread);
    if(b->added) gui_remove_view_port(b->gui, b->vp);
    view_port_free(b->vp);
    furi_record_close(RECORD_GUI);
}

// ---------------------------------------------------------------------------
// fox_progress: reusable determinate "operation in progress" page (see fox_progress.h)
// ---------------------------------------------------------------------------

struct FoxProgress {
    Gui* gui;
    ViewPort* vp;
    const char* title;       // may contain '\n' for multiple lines
    volatile float frac;     // 0..1, drawn quantised to quarters
    volatile bool cancelled; // user pressed Back
};

static void fox_progress_draw(Canvas* canvas, void* ctx) {
    FoxProgress* p = ctx;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    // Fox on the left, facing across toward the title.
    canvas_draw_icon(canvas, 4, 9, &I_fox_facing_32x32);
    // Title: all bold, each line centred in the column to the right of the fox.
    canvas_set_font(canvas, FontPrimary);
    const int cx = 84; // centre of the text column (the fox takes the left)
    const char* s = p->title;
    int ty = 17, idx = 0;
    char line[48];
    while(*s && idx < 4) {
        int n = 0;
        while(s[n] && s[n] != '\n' && n < 47) {
            line[n] = s[n];
            n++;
        }
        line[n] = '\0';
        canvas_draw_str_aligned(canvas, cx, ty, AlignCenter, AlignBottom, line);
        ty += 12;
        idx++;
        s += n;
        if(*s == '\n') s++;
    }
    // Progress bar — the firmware's own rounded bar, exactly like the updater app. Left margin ~30%,
    // width ~60% of the 128px screen; it fills smoothly as frac advances.
    float fr = p->frac;
    if(fr < 0.0f) fr = 0.0f;
    if(fr > 1.0f) fr = 1.0f;
    elements_progress_bar(canvas, 38, 50, 77, fr);
}

static void fox_progress_input(InputEvent* event, void* ctx) {
    FoxProgress* p = ctx;
    if(event->type == InputTypeShort && event->key == InputKeyBack) {
        p->cancelled = true;
    }
}

FoxProgress* fox_progress_begin(const char* title) {
    FoxProgress* p = malloc(sizeof(FoxProgress));
    p->gui = furi_record_open(RECORD_GUI);
    p->vp = view_port_alloc();
    p->title = title;
    p->frac = 0.0f;
    p->cancelled = false;
    view_port_draw_callback_set(p->vp, fox_progress_draw, p);
    view_port_input_callback_set(p->vp, fox_progress_input, p);
    gui_add_view_port(p->gui, p->vp, GuiLayerFullscreen);
    view_port_update(p->vp);
    return p;
}

void fox_progress_set(FoxProgress* p, float frac) {
    if(!p) return;
    p->frac = frac;
    view_port_update(p->vp);
}

bool fox_progress_cancelled(FoxProgress* p) {
    return p && p->cancelled;
}

void fox_progress_end(FoxProgress* p) {
    if(!p) return;
    gui_remove_view_port(p->gui, p->vp);
    view_port_free(p->vp);
    furi_record_close(RECORD_GUI);
    free(p);
}

// ---------------------------------------------------------------------------
// fox_watchdog: hang guard (see fox_watchdog.h)
// ---------------------------------------------------------------------------

#define FOX_HANG_LOG     EXT_PATH("apps_data/fox_companion/hang_log.txt")
#define FOX_WD_INITIAL_MS 20000u // the op must show progress / finish within this of a start or a kick
#define FOX_WD_SLICE_MS   15000u // each kick extends the deadline by this

static FuriThread* g_wd_thread = NULL;
static volatile uint32_t g_wd_deadline = 0;
static volatile bool g_wd_run = false;
static const char* g_wd_what = "";
static uint32_t g_wd_start_tick = 0;

static void fox_watchdog_write_log(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, FOX_DIR);
    File* f = storage_file_alloc(storage);
    if(storage_file_open(f, FOX_HANG_LOG, FSAM_WRITE, FSOM_OPEN_APPEND)) {
        char buf[192];
        int n = snprintf(
            buf,
            sizeof(buf),
            "HANG tick=%lu op=\"%s\" ran_ms=%lu free_heap=%u -> reset\n",
            (unsigned long)furi_get_tick(),
            g_wd_what ? g_wd_what : "?",
            (unsigned long)(furi_get_tick() - g_wd_start_tick),
            (unsigned)memmgr_get_free_heap());
        if(n > 0) storage_file_write(f, buf, (size_t)n);
        storage_file_close(f);
    }
    storage_file_free(f);
    furi_record_close(RECORD_STORAGE);
}

static int32_t fox_watchdog_thread(void* ctx) {
    UNUSED(ctx);
    while(g_wd_run) {
        // Signed compare so tick wraparound is handled.
        if((int32_t)(furi_get_tick() - g_wd_deadline) > 0) {
            // No progress within the window: the operation is hung. Record it, then reset the Flipper
            // so the stuck loader is freed and the companion can be launched again.
            fox_watchdog_write_log();
            furi_crash("Fox companion: operation hang");
            // not reached
        }
        furi_delay_ms(200);
    }
    return 0;
}

void fox_watchdog_start(const char* what) {
    if(g_wd_thread) return; // one at a time
    g_wd_what = what;
    g_wd_start_tick = furi_get_tick();
    g_wd_deadline = furi_get_tick() + furi_ms_to_ticks(FOX_WD_INITIAL_MS);
    g_wd_run = true;
    g_wd_thread = furi_thread_alloc_ex("FoxWdg", 1024, fox_watchdog_thread, NULL);
    furi_thread_start(g_wd_thread);
}

void fox_watchdog_kick(void) {
    g_wd_deadline = furi_get_tick() + furi_ms_to_ticks(FOX_WD_SLICE_MS);
}

void fox_watchdog_stop(void) {
    if(!g_wd_thread) return;
    g_wd_run = false;
    furi_thread_join(g_wd_thread);
    furi_thread_free(g_wd_thread);
    g_wd_thread = NULL;
}

// ---------------------------------------------------------------------------
// PAINT mode: live-mirror the Android painter's 128x64 whiteboard onto the screen.
// Resident (no watchdog, by design — the user holds it open): loops reading paint.bin and redrawing
// until the app touches paint.stop (or the user presses Back on the Flipper).
// ---------------------------------------------------------------------------

typedef struct {
    uint8_t buf[1024]; // 128x64 @ 1bpp, XBM bit order (matches canvas_draw_xbm + the app's painter)
    volatile bool back;
} FoxPaint;

static void fox_paint_draw(Canvas* canvas, void* ctx) {
    FoxPaint* pt = ctx;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_xbm(canvas, 0, 0, 128, 64, pt->buf);
}

static void fox_paint_input(InputEvent* event, void* ctx) {
    FoxPaint* pt = ctx;
    // Exit on EITHER a short tap or a long hold of Back. The Flipper loader never force-kills a running
    // FAP (it only reacts once our entry function returns), so a long-Back would otherwise do nothing and
    // leave PAINT resident. Handling both gestures guarantees any Back press falls through to the single
    // clean teardown below (gui_remove_view_port + view_port_free), so nothing is ever left in the GUI.
    if(event->key == InputKeyBack &&
       (event->type == InputTypeShort || event->type == InputTypeLong)) {
        pt->back = true;
    }
}

static int32_t fox_paint_loop(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, FOX_DIR);
    storage_simply_remove(storage, FOX_PAINT_STOP); // clear a stale stop flag
    storage_simply_remove(storage, FOX_PAINT_GONE); // we're running now -> not gone

    FoxPaint* pt = malloc(sizeof(FoxPaint));
    memset(pt->buf, 0, sizeof(pt->buf));
    pt->back = false;

    Gui* gui = furi_record_open(RECORD_GUI);
    ViewPort* vp = view_port_alloc();
    view_port_draw_callback_set(vp, fox_paint_draw, pt);
    view_port_input_callback_set(vp, fox_paint_input, pt);
    gui_add_view_port(gui, vp, GuiLayerFullscreen);

    while(!pt->back) {
        File* f = storage_file_alloc(storage);
        if(storage_file_open(f, FOX_PAINT_BIN, FSAM_READ, FSOM_OPEN_EXISTING)) {
            storage_file_read(f, pt->buf, sizeof(pt->buf));
            storage_file_close(f);
            view_port_update(vp);
        }
        storage_file_free(f);
        if(storage_file_exists(storage, FOX_PAINT_STOP)) break;
        furi_delay_ms(120);
    }

    storage_simply_remove(storage, FOX_PAINT_STOP);
    // Mark that we've left paint mode (Back pressed), so the app doesn't blindly relaunch us — it
    // reopens only when the user draws again. The app clears this marker when it relaunches us.
    File* gone = storage_file_alloc(storage);
    if(storage_file_open(gone, FOX_PAINT_GONE, FSAM_WRITE, FSOM_CREATE_ALWAYS)) storage_file_close(gone);
    storage_file_free(gone);
    gui_remove_view_port(gui, vp);
    view_port_free(vp);
    furi_record_close(RECORD_GUI);
    free(pt);
    furi_record_close(RECORD_STORAGE);
    return 0;
}

// ---------------------------------------------------------------------------
// output helpers
// ---------------------------------------------------------------------------

// Append src to out, dropping ANSI CSI escape sequences (ESC '[' ... final-byte) and carriage
// returns so the phone sees clean text; newlines are kept.
static void fox_append_stripped(FuriString* out, const uint8_t* src, size_t n) {
    size_t i = 0;
    while(i < n) {
        uint8_t c = src[i];
        if(c == 0x1B) { // ESC
            i++;
            if(i < n && src[i] == '[') {
                i++;
                while(i < n) {
                    uint8_t f = src[i++];
                    if(f >= 0x40 && f <= 0x7E) break; // CSI final byte
                }
            }
            continue;
        }
        if(c == '\r') {
            i++;
            continue;
        }
        if(furi_string_size(out) < OUT_CAP) furi_string_push_back(out, (char)c);
        i++;
    }
}

static void fox_write_out(Storage* s, FuriString* out) {
    File* f = storage_file_alloc(s);
    if(storage_file_open(f, FOX_OUT, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        if(furi_string_size(out) == 0) {
            const char* m = "[companion] (no output)\n";
            storage_file_write(f, m, strlen(m));
        } else {
            const char* d = furi_string_get_cstr(out);
            size_t len = furi_string_size(out), off = 0;
            while(off < len) {
                size_t c = len - off;
                if(c > 512) c = 512;
                if(storage_file_write(f, d + off, c) != c) break;
                off += c;
            }
        }
    }
    storage_file_close(f);
    storage_file_free(f);
}

// ---------------------------------------------------------------------------
// CLI channel — run one command through a real cli_shell on a pipe
// ---------------------------------------------------------------------------

// cli_shell_alloc furi_check()s a non-null motd; we want no banner, so this is a no-op.
static void fox_cli_motd(void* ctx) {
    UNUSED(ctx);
}

// Run `command` and collect its output (ANSI-stripped) into out. The shell runs on the shared main
// CLI registry (RECORD_CLI) so it has the full command set — system builtins (device_info, storage,
// ps, free, ...), shell builtins (help/exit) and external cli_ plugins — exactly like the USB CLI.
// `exit` is appended so the shell thread terminates and can be joined + freed safely
// (cli_shell_free -> furi_thread_free requires a stopped thread).
static void fox_cli_run(const char* command, uint32_t capture_ms, FuriString* out) {
    CliRegistry* registry = furi_record_open(RECORD_CLI);

    PipeSideBundle b = pipe_alloc(4096, 1);
    PipeSide* ours = b.alices_side; // daemon writes commands / reads output here
    PipeSide* shell_side = b.bobs_side; // the shell's stdio

    CliShell* shell = cli_shell_alloc(fox_cli_motd, NULL, shell_side, registry, &FOX_CLI_EXT);
    cli_shell_start(shell);

    pipe_send(ours, command, strlen(command));
    pipe_send(ours, "\r\nexit\r\n", 8);

    uint8_t buf[256];
    const uint32_t start = furi_get_tick();
    const uint32_t dur = furi_ms_to_ticks(capture_ms);
    const uint32_t quiet = furi_ms_to_ticks(300);
    uint32_t last_rx = start;
    bool got_any = false;

    while(true) {
        if(g_cancel) break; // user hit Back on the busy screen — bail now
        fox_watchdog_kick(); // the shell is alive and we're servicing it — not a hang
        size_t avail = pipe_bytes_available(ours);
        if(avail) {
            size_t want = avail < sizeof(buf) ? avail : sizeof(buf);
            size_t n = pipe_receive(ours, buf, want);
            fox_append_stripped(out, buf, n);
            last_rx = furi_get_tick();
            got_any = true;
            continue;
        }
        uint32_t now = furi_get_tick();
        if((uint32_t)(now - start) > dur) break; // hard cap
        if(got_any && (uint32_t)(now - last_rx) > quiet) break; // settled after producing output
        furi_delay_ms(15);
    }

    cli_shell_join(shell); // `exit` stopped the thread; join is then instant and makes free() safe

    // final drain of anything buffered after the last read
    size_t n;
    while(pipe_bytes_available(ours) > 0 && (n = pipe_receive(ours, buf, sizeof(buf))) > 0) {
        fox_append_stripped(out, buf, n);
    }

    cli_shell_free(shell);
    pipe_free(ours);
    pipe_free(shell_side);
    furi_record_close(RECORD_CLI); // shared registry — close the record, never free it
}

// ---------------------------------------------------------------------------
// ESP32 channel — GPIO UART relay (resident serial, acquired lazily)
// ---------------------------------------------------------------------------

typedef struct {
    FuriHalSerialHandle* serial;
    FuriStreamBuffer* rx;
    bool up;
    Expansion* expansion; // expansion-module service, suspended while we drive the UART
    bool owned;           // true if we initialised the USART peripheral (vs. it was already up)
} EspCtx;

static void fox_esp_on_irq(FuriHalSerialHandle* h, FuriHalSerialRxEvent ev, void* ctx) {
    EspCtx* e = ctx;
    if(ev & FuriHalSerialRxEventData) {
        uint8_t byte = furi_hal_serial_async_rx(h);
        furi_stream_buffer_send(e->rx, &byte, 1, 0);
    }
}

static bool fox_esp_init(EspCtx* e) {
    if(e->up) return true;
    // The firmware's expansion-module service drives this very USART (pins 13/14) whenever a board is
    // attached, so it is actively polling the line when an ESP32 is present. Acquiring the UART without
    // first suspending that service races it and trips a furi_check() — exactly the intermittent crash
    // seen only with an ESP32 connected. The Fox ESP32 Detector (and every other Fox ESP app) disables
    // expansion first; do the same, and re-enable it in fox_esp_deinit.
    e->expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(e->expansion);

    e->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(!e->serial) {
        expansion_enable(e->expansion);
        furi_record_close(RECORD_EXPANSION);
        e->expansion = NULL;
        return false;
    }
    e->rx = furi_stream_buffer_alloc(RX_STREAM_SZ, 1);
    // Initialising an already-enabled peripheral also furi_check()s, so only init when we own the bus.
    e->owned = !furi_hal_bus_is_enabled(FuriHalBusUSART1);
    if(e->owned) furi_hal_serial_init(e->serial, UART_BAUD);
    // Set the baud explicitly like the detector does — when the bus was already up we skip init above,
    // so without this the UART could sit at the wrong rate and the probe reads garbage.
    furi_hal_serial_set_br(e->serial, UART_BAUD);
    furi_hal_serial_async_rx_start(e->serial, fox_esp_on_irq, e, false);
    e->up = true;
    return true;
}

static void fox_esp_deinit(EspCtx* e) {
    if(!e->up) return;
    furi_hal_serial_async_rx_stop(e->serial);
    if(e->owned) furi_hal_serial_deinit(e->serial);
    furi_hal_serial_control_release(e->serial);
    if(e->expansion) {
        expansion_enable(e->expansion); // hand the UART back to the expansion-module service
        furi_record_close(RECORD_EXPANSION);
        e->expansion = NULL;
    }
    furi_stream_buffer_free(e->rx);
    e->serial = NULL;
    e->rx = NULL;
    e->up = false;
    e->owned = false;
}

static void fox_esp_run(EspCtx* e, const char* line, uint32_t capture_ms, FuriString* out) {
    if(!fox_esp_init(e)) {
        furi_string_cat_str(out, "[esp32] UART busy (another app holds it)\n");
        return;
    }
    uint8_t b;
    while(furi_stream_buffer_receive(e->rx, &b, 1, 0) > 0) {} // flush stale RX
    if(line[0] != '\0') {
        furi_hal_serial_tx(e->serial, (const uint8_t*)line, strlen(line));
        // CRLF, matching fox_esp32_detector: the Fox ESP32 firmware (and ESP-AT) only act on \r\n.
        furi_hal_serial_tx(e->serial, (const uint8_t*)"\r\n", 2);
        furi_hal_serial_tx_wait_complete(e->serial);
    }
    uint8_t buf[128];
    const uint32_t start = furi_get_tick();
    const uint32_t dur = furi_ms_to_ticks(capture_ms);
    while((uint32_t)(furi_get_tick() - start) < dur) {
        if(g_cancel) break; // user hit Back on the busy screen — stop capturing now
        fox_watchdog_kick(); // UART capture in progress — healthy, keep the watchdog from resetting us
        size_t n = furi_stream_buffer_receive(e->rx, buf, sizeof(buf), furi_ms_to_ticks(40));
        for(size_t i = 0; i < n; i++) {
            if(furi_string_size(out) >= OUT_CAP) break;
            furi_string_push_back(out, (char)buf[i]);
        }
    }
}

// ---------------------------------------------------------------------------
// ESP32 identification — ported from fox_esp32_detector's proven fingerprint.
// ---------------------------------------------------------------------------

// Case-insensitive substring search (lifted from fox_esp32_detector/main.c).
static bool fox_contains_ci(const char* haystack, const char* needle) {
    size_t hl = strlen(haystack);
    size_t nl = strlen(needle);
    if(nl == 0 || nl > hl) return false;
    for(size_t i = 0; i + nl <= hl; i++) {
        size_t j = 0;
        for(; j < nl; j++) {
            char a = haystack[i + j];
            char b = needle[j];
            if(a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if(b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if(a != b) break;
        }
        if(j == nl) return true;
    }
    return false;
}

// Classify whatever is on the GPIO UART the way the Fox ESP32 Detector does:
//   "fox"    a Fox ESP32 Firmware banner answered our "info" probe
//   "notfox" a recognized but non-Fox ESP32 (Bruce, or a bare ESP-AT firmware)
//   "none"   nothing recognizable: silence, or garbage from a board that's in
//            another mode (e.g. CC1101) -> the DEVICE page shows this as WAITING
// Key point: raw/garbage bytes alone are NEVER "notfox"; only a firmware we can
// actually name counts. This is what stops a CC1101-mode board reading NOT FOXFW.
static const char* fox_esp_classify(EspCtx* e) {
    FuriString* r = furi_string_alloc();
    const char* verdict = NULL;

    // 1) "info" — both Fox and Bruce answer with an identifiable banner.
    fox_esp_run(e, "info", 600, r);
    const char* s = furi_string_get_cstr(r);
    if(fox_contains_ci(s, "fox esp32 firmware")) {
        verdict = "fox";
    } else if(fox_contains_ci(s, "bruce")) {
        verdict = "notfox";
    }

    // 2) Fall back to ESP-AT: a bare ESP32 answers "AT" with an OK line and
    //    "AT+GMR" with an "AT version" banner. Require a real token so random
    //    garbage can't masquerade as a firmware.
    if(verdict == NULL) {
        furi_string_reset(r);
        fox_esp_run(e, "AT", 300, r);
        furi_string_cat_str(r, "|");
        fox_esp_run(e, "AT+GMR", 400, r);
        s = furi_string_get_cstr(r);
        if(fox_contains_ci(s, "AT version") || fox_contains_ci(s, "OK\r") ||
           fox_contains_ci(s, "OK\n")) {
            verdict = "notfox";
        }
    }

    furi_string_free(r);
    return verdict ? verdict : "none";
}

// ---------------------------------------------------------------------------
// command file parsing + main loop
// ---------------------------------------------------------------------------

// Reads cmd.txt: line 1 = "<CHAN> <payload>", line 2 = captureMs. Returns false if absent.
static bool fox_read_cmd(Storage* s, FuriString* line, uint32_t* capture_ms) {
    File* f = storage_file_alloc(s);
    bool ok = false;
    if(storage_file_open(f, FOX_CMD, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char raw[1024];
        uint16_t rd = storage_file_read(f, raw, sizeof(raw) - 1);
        raw[rd] = '\0';
        const char* nl = strchr(raw, '\n');
        size_t len = nl ? (size_t)(nl - raw) : strlen(raw);
        furi_string_set_strn(line, raw, len);
        *capture_ms = 2500;
        if(nl) {
            int ms = atoi(nl + 1);
            if(ms >= 100 && ms <= 30000) *capture_ms = (uint32_t)ms;
        }
        ok = true;
    }
    storage_file_close(f);
    storage_file_free(f);
    return ok;
}

// Split "<CHAN> <payload>" at the first space. chan gets the prefix; returns the payload pointer.
static const char* fox_split_channel(const char* s, char* chan, size_t chansz) {
    size_t i = 0;
    while(s[i] && s[i] != ' ' && i < chansz - 1) {
        chan[i] = s[i];
        i++;
    }
    chan[i] = '\0';
    if(s[i] == ' ') return s + i + 1;
    return s + i; // no payload
}

// One-shot launch helpers: the app appStarts us with the op as the arg for SubGHz/NFC (like the
// old single-purpose FAPs). These detect that form and the SubGHz status write the app reads back.
static bool fox_starts_with(const char* s, const char* pfx) {
    return strncmp(s, pfx, strlen(pfx)) == 0;
}

static bool fox_is_subghz_arg(const char* a) {
    return fox_starts_with(a, "RX ") || fox_starts_with(a, "TX ") || fox_starts_with(a, "SWEEP ");
}

static bool fox_is_nfc_arg(const char* a) {
    return fox_starts_with(a, "READ_HF ") || fox_starts_with(a, "EMU_HF ") ||
           fox_starts_with(a, "WRITE_HF ") || fox_starts_with(a, "READ_LF ") ||
           fox_starts_with(a, "EMU_LF ") || fox_starts_with(a, "WRITE_LF ");
}

static void fox_write_io_status(int32_t rc) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, FOX_DIR);
    File* f = storage_file_alloc(storage);
    if(storage_file_open(f, FOX_IOSTATUS, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char buf[16];
        int n = snprintf(buf, sizeof(buf), "%ld\n", (long)rc);
        storage_file_write(f, buf, (size_t)n);
        storage_file_close(f);
    }
    storage_file_free(f);
    furi_record_close(RECORD_STORAGE);
}

// Count installed .fap apps under /ext/apps (one level of category folders), bounded depth so an odd
// tree can't loop us. Feeds the DEVICE "Installed apps" tile via RXSTATUS.
static uint32_t fox_count_faps(Storage* storage, const char* dir, int depth) {
    if(depth > 2) return 0;
    uint32_t n = 0;
    File* d = storage_file_alloc(storage);
    if(storage_dir_open(d, dir)) {
        FileInfo fi;
        char name[128];
        while(storage_dir_read(d, &fi, name, sizeof(name))) {
            if(file_info_is_dir(&fi)) {
                char sub[256];
                snprintf(sub, sizeof(sub), "%s/%s", dir, name);
                n += fox_count_faps(storage, sub, depth + 1);
            } else {
                size_t l = strlen(name);
                if(l > 4 && name[l - 4] == '.' && (name[l - 3] == 'f' || name[l - 3] == 'F') &&
                   (name[l - 2] == 'a' || name[l - 2] == 'A') &&
                   (name[l - 1] == 'p' || name[l - 1] == 'P'))
                    n++;
            }
        }
    }
    storage_dir_close(d);
    storage_file_free(d);
    return n;
}

int32_t fox_android_companion_app(void* p) {
    // The app launches us with the data dir as the arg; opened by hand there's no arg -> show a
    // short "working" splash so a curious user gets confirmation, then exit (it's headless).
    const char* launch_args = (const char*)p;
    bool launched_by_app = launch_args && launch_args[0] != '\0';

    if(!launched_by_app) {
        fox_manual_splash();
        return 0;
    }

    // Desktop flag-launch for a settings sync: service request.flag headless and exit.
    if(strncmp(launch_args, "SETTINGS", 8) == 0) {
        fox_settings_process();
        return 0;
    }

    // PAINT: resident live mirror of the Android painter's whiteboard. No watchdog — the user holds it
    // open deliberately; it exits when the app touches paint.stop or the user presses Back.
    if(strncmp(launch_args, "PAINT", 5) == 0) {
        return fox_paint_loop();
    }

    // One-shot per-op launches (SubGHz / NFC): the app appStarts us with the op as the arg,
    // exactly like the old single-purpose FAPs. Run it, write status, exit.
    if(fox_is_subghz_arg(launch_args)) {
        fox_watchdog_start(launch_args); // force-reset + hang log if a capture/TX ever blocks
        int32_t rc = fox_subghz_channel(launch_args);
        fox_watchdog_stop();
        fox_write_io_status(rc);
        return rc;
    }
    if(fox_is_nfc_arg(launch_args)) {
        fox_nfc_channel(launch_args); // writes nfc_status.txt itself
        return 0;
    }

    // Flipper-side firmware install: "UPDATE <tarpath>" extracts the .tar and hands off to updater_app.
    if(fox_starts_with(launch_args, "UPDATE ")) {
        int32_t rc = fox_update_channel(launch_args + 7);
        fox_write_io_status(rc);
        return rc;
    }

    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, FOX_DIR);

    EspCtx esp = {0};
    FuriString* cmdline = furi_string_alloc();
    uint32_t capture_ms = 0;

    // One-shot: the app writes cmd.txt then launches us. We wait briefly for it (filesystem latency),
    // run exactly ONE command, write out.txt, then EXIT so the Flipper's loader is free again. The app
    // re-launches us per command — nothing stays resident, so the Flipper is usable between operations.
    bool got = false;
    const uint32_t deadline = furi_get_tick() + furi_ms_to_ticks(1500);
    while(furi_get_tick() < deadline) {
        if(fox_read_cmd(storage, cmdline, &capture_ms)) {
            got = true;
            break;
        }
        furi_delay_ms(40);
    }

    if(got) {
        storage_simply_remove(storage, FOX_CMD);

        char chan[16];
        const char* payload = fox_split_channel(furi_string_get_cstr(cmdline), chan, sizeof(chan));

        // Start watching the op. The fox/"in progress" screen only appears if it actually runs past
        // 3s (and the buttons past 4s); anything quicker stays fully invisible — see fox_busy_thread.
        BusyUi busy;
        fox_busy_start(&busy);

        // Hang watchdog over the dispatch. Previously only the SubGHz-arg appStart path had one, so a
        // wedged CLI/ESP32/NFC op on the cmd.txt path could block forever with no reset — and a
        // force-close then would abandon e.g. the CLI shell thread on the SHARED cli registry, stranding
        // its allocations (permanent fragmentation). The capture/emulate loops kick it, so legit long
        // windows (up to the 30s cap) ride through; only a true hang (no progress for 20s, e.g.
        // cli_shell_join never returning on a streaming command) trips it into a clean reset.
        // UPDATE (tar extract / updater handoff) and SETTINGS legitimately run long with no shared-service
        // strand to protect, and must never be reset mid-flight, so they opt out.
        bool wd_guarded = strcmp(chan, "UPDATE") != 0 && strcmp(chan, "SETTINGS") != 0;
        if(wd_guarded) fox_watchdog_start(chan);

        FuriString* out = furi_string_alloc();
        if(strcmp(chan, "CLI") == 0) {
            fox_cli_run(payload, capture_ms, out);
        } else if(strcmp(chan, "ESP32") == 0) {
            fox_esp_run(&esp, payload, capture_ms, out);
        } else if(strcmp(chan, "SUBGHZ") == 0) {
            int32_t rc = fox_subghz_channel(payload);
            furi_string_printf(out, rc == 0 ? "ok\n" : "err %ld\n", (long)rc);
        } else if(strcmp(chan, "NFC") == 0) {
            furi_string_printf(out, "%s\n", fox_nfc_channel(payload));
        } else if(strcmp(chan, "SETTINGS") == 0) {
            fox_settings_process();
            furi_string_set_str(out, "ok\n");
        } else if(strcmp(chan, "UPDATE") == 0) {
            int32_t rc = fox_update_channel(payload);
            furi_string_printf(out, rc == 0 ? "ok\n" : "err %ld\n", (long)rc);
        } else if(strcmp(chan, "RXSTATUS") == 0) {
            // DEVICE status in ONE shot: receiver probes + SD usage + installed app count, so the app's
            // tiles populate together instead of staggering across separate calls. (Battery + hardware/
            // radio come over fast RPC on the app side, so they are not repeated here.)
            bool ext = fox_subghz_external_present();
            const char* esp_s = fox_esp_classify(&esp);
            size_t protos = fox_subghz_protocol_count();
            uint64_t sd_total = 0, sd_free = 0;
            storage_common_fs_info(storage, STORAGE_EXT_PATH_PREFIX, &sd_total, &sd_free);
            uint64_t sd_used = (sd_total > sd_free) ? (sd_total - sd_free) : 0;
            uint32_t sd_used_kb = (uint32_t)(sd_used / 1024u);
            uint32_t sd_total_kb = (uint32_t)(sd_total / 1024u);
            uint32_t apps = fox_count_faps(storage, EXT_PATH("apps"), 0);
            furi_string_printf(
                out,
                "internal=ready cc1101=%s esp32=%s protocols=%u apps=%lu sd_used_kb=%lu sd_total_kb=%lu\n",
                ext ? "ready" : "wait", esp_s, (unsigned)protos, (unsigned long)apps,
                (unsigned long)sd_used_kb, (unsigned long)sd_total_kb);
        } else {
            furi_string_printf(out, "[companion] unknown channel: %s\n", chan);
        }

        fox_watchdog_stop(); // op returned normally — disarm before it can trip

        // If the user cancelled, tell the app so plainly — it watches for this marker to stop any
        // multi-step operation (e.g. a chunked handshake capture) and hold there until the user asks
        // to retry, instead of firing the next step.
        if(g_cancel) furi_string_cat_str(out, "\n[cancelled]\n");

        fox_busy_stop(&busy); // tear down the screen -> desktop returns instantly

        fox_write_out(storage, out);
        furi_string_free(out);
    }

    fox_esp_deinit(&esp);
    furi_string_free(cmdline);
    furi_record_close(RECORD_STORAGE);
    return 0;
}
