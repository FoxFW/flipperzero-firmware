

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
#include <cli/cli.h>
#include "fox_android_companion_icons.h"
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
#define FOX_PAINT_BIN  EXT_PATH("apps_data/fox_companion/paint.bin")
#define FOX_PAINT_STOP EXT_PATH("apps_data/fox_companion/paint.stop")
#define FOX_PAINT_GONE EXT_PATH("apps_data/fox_companion/paint.gone")
#define FOX_IOSTATUS EXT_PATH("apps_data/fox_companion/io_status.txt")

#define OUT_CAP 60000u

#define UART_BAUD    115200u
#define RX_STREAM_SZ 8192

static const CliCommandExternalConfig FOX_CLI_EXT = {
    .search_directory = "/ext/apps_data/cli/plugins",
    .fal_prefix = "cli_",
    .appid = "cli",
};

typedef struct {
    int phase;
    uint8_t frame;
} Splash;

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
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "App is Running!");
    }
}

static void fox_manual_splash(void) {
    Gui* gui = furi_record_open(RECORD_GUI);
    ViewPort* vp = view_port_alloc();
    Splash s = {.phase = 0, .frame = 0};
    view_port_draw_callback_set(vp, fox_splash_draw, &s);
    gui_add_view_port(gui, vp, GuiLayerFullscreen);

    for(uint32_t t = 0; t < 2000u; t += 50u) {
        s.frame = (uint8_t)((s.frame + 1u) % 8u);
        view_port_update(vp);
        furi_delay_ms(50);
    }

    s.phase = 1;
    view_port_update(vp);
    furi_delay_ms(2000);

    gui_remove_view_port(gui, vp);
    view_port_free(vp);
    furi_record_close(RECORD_GUI);
}

static volatile bool g_cancel = false;

typedef struct {
    Gui* gui;
    ViewPort* vp;
    FuriThread* thread;
    volatile int phase;
    volatile bool added;
    volatile bool done;
} BusyUi;

static void fox_busy_draw(Canvas* canvas, void* ctx) {
    BusyUi* b = ctx;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_draw_icon(canvas, 4, 9, &I_fox_right_32x32);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 42, 18, "Android App");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 42, 30, "Operation");
    canvas_draw_str(canvas, 42, 40, "in progress!");
    if(b->phase >= 2) {

        canvas_draw_rframe(canvas, 4, 49, 54, 13, 3);
        canvas_draw_str_aligned(canvas, 31, 55, AlignCenter, AlignCenter, "OK: Wait");
        canvas_draw_rbox(canvas, 70, 49, 54, 13, 3);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str_aligned(canvas, 97, 55, AlignCenter, AlignCenter, "Back: Cancel");
    }
}

static void fox_busy_input(InputEvent* event, void* ctx) {
    BusyUi* b = ctx;

    if(b->phase >= 2 && event->type == InputTypeShort && event->key == InputKeyBack) {
        g_cancel = true;
    }
}

static int32_t fox_busy_thread(void* ctx) {
    BusyUi* b = ctx;

    for(uint32_t t = 0; t < 3000u && !b->done; t += 20u) furi_delay_ms(20);
    if(b->done) return 0;
    b->phase = 1;
    gui_add_view_port(b->gui, b->vp, GuiLayerFullscreen);
    b->added = true;
    view_port_update(b->vp);

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

struct FoxProgress {
    Gui* gui;
    ViewPort* vp;
    const char* title;
    volatile float frac;
    volatile bool cancelled;
};

static void fox_progress_draw(Canvas* canvas, void* ctx) {
    FoxProgress* p = ctx;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_draw_icon(canvas, 4, 9, &I_fox_facing_32x32);

    canvas_set_font(canvas, FontPrimary);
    const int cx = 84;
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

#define FOX_HANG_LOG     EXT_PATH("apps_data/fox_companion/hang_log.txt")
#define FOX_WD_INITIAL_MS 20000u
#define FOX_WD_SLICE_MS   15000u

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

        if((int32_t)(furi_get_tick() - g_wd_deadline) > 0) {

            fox_watchdog_write_log();
            furi_crash("Fox companion: operation hang");

        }
        furi_delay_ms(200);
    }
    return 0;
}

void fox_watchdog_start(const char* what) {
    if(g_wd_thread) return;
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

typedef struct {
    uint8_t buf[1024];
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

    if(event->key == InputKeyBack &&
       (event->type == InputTypeShort || event->type == InputTypeLong)) {
        pt->back = true;
    }
}

static int32_t fox_paint_loop(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, FOX_DIR);
    storage_simply_remove(storage, FOX_PAINT_STOP);
    storage_simply_remove(storage, FOX_PAINT_GONE);

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

static void fox_append_stripped(FuriString* out, const uint8_t* src, size_t n) {
    size_t i = 0;
    while(i < n) {
        uint8_t c = src[i];
        if(c == 0x1B) {
            i++;
            if(i < n && src[i] == '[') {
                i++;
                while(i < n) {
                    uint8_t f = src[i++];
                    if(f >= 0x40 && f <= 0x7E) break;
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

static void fox_cli_motd(void* ctx) {
    UNUSED(ctx);
}

static void fox_cli_run(const char* command, uint32_t capture_ms, FuriString* out) {
    CliRegistry* registry = furi_record_open(RECORD_CLI);

    PipeSideBundle b = pipe_alloc(4096, 1);
    PipeSide* ours = b.alices_side;
    PipeSide* shell_side = b.bobs_side;

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
        if(g_cancel) break;
        fox_watchdog_kick();
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
        if((uint32_t)(now - start) > dur) break;
        if(got_any && (uint32_t)(now - last_rx) > quiet) break;
        furi_delay_ms(15);
    }

    cli_shell_join(shell);

    size_t n;
    while(pipe_bytes_available(ours) > 0 && (n = pipe_receive(ours, buf, sizeof(buf))) > 0) {
        fox_append_stripped(out, buf, n);
    }

    cli_shell_free(shell);
    pipe_free(ours);
    pipe_free(shell_side);
    furi_record_close(RECORD_CLI);
}

typedef struct {
    FuriHalSerialHandle* serial;
    FuriStreamBuffer* rx;
    bool up;
    Expansion* expansion;
    bool owned;
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

    e->owned = !furi_hal_bus_is_enabled(FuriHalBusUSART1);
    if(e->owned) furi_hal_serial_init(e->serial, UART_BAUD);

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
        expansion_enable(e->expansion);
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
    while(furi_stream_buffer_receive(e->rx, &b, 1, 0) > 0) {}
    if(line[0] != '\0') {
        furi_hal_serial_tx(e->serial, (const uint8_t*)line, strlen(line));

        furi_hal_serial_tx(e->serial, (const uint8_t*)"\r\n", 2);
        furi_hal_serial_tx_wait_complete(e->serial);
    }
    uint8_t buf[128];
    const uint32_t start = furi_get_tick();
    const uint32_t dur = furi_ms_to_ticks(capture_ms);
    while((uint32_t)(furi_get_tick() - start) < dur) {
        if(g_cancel) break;
        fox_watchdog_kick();
        size_t n = furi_stream_buffer_receive(e->rx, buf, sizeof(buf), furi_ms_to_ticks(40));
        for(size_t i = 0; i < n; i++) {
            if(furi_string_size(out) >= OUT_CAP) break;
            furi_string_push_back(out, (char)buf[i]);
        }
    }
}

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

static const char* fox_esp_classify(EspCtx* e) {
    FuriString* r = furi_string_alloc();
    const char* verdict = NULL;

    fox_esp_run(e, "info", 600, r);
    const char* s = furi_string_get_cstr(r);
    if(fox_contains_ci(s, "fox esp32 firmware")) {
        verdict = "fox";
    } else if(fox_contains_ci(s, "bruce")) {
        verdict = "notfox";
    }

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

static const char* fox_split_channel(const char* s, char* chan, size_t chansz) {
    size_t i = 0;
    while(s[i] && s[i] != ' ' && i < chansz - 1) {
        chan[i] = s[i];
        i++;
    }
    chan[i] = '\0';
    if(s[i] == ' ') return s + i + 1;
    return s + i;
}

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

    const char* launch_args = (const char*)p;
    bool launched_by_app = launch_args && launch_args[0] != '\0';

    if(!launched_by_app) {
        fox_manual_splash();
        return 0;
    }

    if(strncmp(launch_args, "SETTINGS", 8) == 0) {
        fox_settings_process();
        return 0;
    }

    if(strncmp(launch_args, "PAINT", 5) == 0) {
        return fox_paint_loop();
    }

    if(fox_is_subghz_arg(launch_args)) {
        fox_watchdog_start(launch_args);
        int32_t rc = fox_subghz_channel(launch_args);
        fox_watchdog_stop();
        fox_write_io_status(rc);
        return rc;
    }
    if(fox_is_nfc_arg(launch_args)) {
        fox_nfc_channel(launch_args);
        return 0;
    }

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

        BusyUi busy;
        fox_busy_start(&busy);

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

        fox_watchdog_stop();

        if(g_cancel) furi_string_cat_str(out, "\n[cancelled]\n");

        fox_busy_stop(&busy);

        fox_write_out(storage, out);
        furi_string_free(out);
    }

    fox_esp_deinit(&esp);
    furi_string_free(cmdline);
    furi_record_close(RECORD_STORAGE);
    return 0;
}
