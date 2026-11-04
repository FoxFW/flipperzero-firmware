#include <furi.h>
#include <furi_hal.h>
#include <expansion/expansion.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/widget.h>
#include <gui/icon_i.h>
#include "fox_esp32_detector_icons.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#define LINE_MAX 128

#define AT_TIMEOUT_MS       600
#define GMR_TIMEOUT_MS      1200
#define FINGERPRINT_TIMEOUT_MS 400
#define NMEA_LISTEN_MS      200
#define CAPTURE_MAX         160

#define FLOOD_LISTEN_MS  900
#define FLOOD_MIN_BYTES  24

#define BANNER_BUFFER_MAX 160

typedef enum {
    DetectorViewResult,
    DetectorViewProgress,
    DetectorViewResultTable,
} DetectorView;

typedef enum {
    DetectorEventProgress,
} DetectorEvent;

typedef struct {
    FuriHalSerialId serial_id;
    const char* label;
} PinOption;

static const PinOption pin_options[] = {
    {FuriHalSerialIdUsart, "13/14 (USART)"},
    {FuriHalSerialIdLpuart, "15/16 (LPUART)"},
};
#define PIN_OPTION_COUNT (sizeof(pin_options) / sizeof(pin_options[0]))

static const uint32_t baud_options[] =
    {115200, 9600, 19200, 38400, 57600, 74880, 230400, 460800, 921600};
#define BAUD_OPTION_COUNT (sizeof(baud_options) / sizeof(baud_options[0]))

typedef struct {
    const char* raw_marker;
    const char* clean_name;
} KnownTag;

static const KnownTag known_tags[] = {
    {"bruce", "Bruce firmware"},
    {"fox esp32 firmware", "Fox ESP32 Firmware"},
};
#define KNOWN_TAG_COUNT (sizeof(known_tags) / sizeof(known_tags[0]))

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Widget* widget;
    View* progress_view;
    View* result_view;

    FuriMutex* mutex;
    FuriThread* scan_thread;
    bool scanning;

    volatile bool cancel_requested;

    size_t progress_index;
    size_t progress_total;
    char progress_status[64];

    size_t progress_display_index;
    size_t progress_display_total;
    char progress_display_status[64];

    bool scan_done;
    bool result_found;
    bool result_any_pin_claimed;
    char result_banner[LINE_MAX];
    size_t result_pin;
    uint32_t result_baud;

    bool result_flood_detected;
    size_t result_flood_pin;
} App;

typedef struct {
    FuriStreamBuffer* rx_stream;
    FuriHalSerialHandle* serial;
    volatile bool* cancel;
} Link;

static void link_rx_callback(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* context) {
    UNUSED(handle);
    Link* link = context;
    if(event == FuriHalSerialRxEventData) {
        uint8_t byte = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(link->rx_stream, &byte, 1, 0);
    }
}

static bool link_read_line_until(Link* link, char* out, size_t out_capacity, uint32_t deadline_tick) {
    size_t len = 0;
    while(furi_get_tick() < deadline_tick) {
        if(link->cancel != NULL && *link->cancel) return false;
        uint8_t byte;
        size_t got = furi_stream_buffer_receive(link->rx_stream, &byte, 1, 50);
        if(got == 0) continue;

        if(byte == '\n') {
            if(len > 0 && out[len - 1] == '\r') len--;
            out[len] = '\0';
            if(len > 0) return true;
            len = 0;
            continue;
        }
        if(len < out_capacity - 1) out[len++] = (char)byte;
    }
    return false;
}

static bool link_expect_ok(Link* link, uint32_t timeout_ms) {
    char line[LINE_MAX];
    uint32_t deadline = furi_get_tick() + timeout_ms;
    while(link_read_line_until(link, line, sizeof(line), deadline)) {
        if(strcmp(line, "OK") == 0) return true;
        if(strcmp(line, "ERROR") == 0) return false;
    }
    return false;
}

static bool
    link_get_firmware_banner(Link* link, char* banner_out, size_t banner_capacity, uint32_t timeout_ms) {
    char line[LINE_MAX];
    uint32_t deadline = furi_get_tick() + timeout_ms;
    bool found = false;
    while(link_read_line_until(link, line, sizeof(line), deadline)) {
        if(strstr(line, "AT version") != NULL) {
            strncpy(banner_out, line, banner_capacity - 1);
            banner_out[banner_capacity - 1] = '\0';
            found = true;
        }
        if(strcmp(line, "OK") == 0 || strcmp(line, "ERROR") == 0) break;
    }
    return found;
}

static FuriHalBus link_bus_for_serial(FuriHalSerialId serial_id) {
    return serial_id == FuriHalSerialIdUsart ? FuriHalBusUSART1 : FuriHalBusLPUART1;
}

static bool contains_ci(const char* haystack, const char* needle) {
    size_t haystack_len = strlen(haystack);
    size_t needle_len = strlen(needle);
    if(needle_len == 0 || needle_len > haystack_len) return false;

    for(size_t i = 0; i + needle_len <= haystack_len; i++) {
        size_t j = 0;
        for(; j < needle_len; j++) {
            char a = haystack[i + j];
            char b = needle[j];
            if(a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if(b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if(a != b) break;
        }
        if(j == needle_len) return true;
    }
    return false;
}

static const char* lookup_known_tag(const char* raw) {
    for(size_t i = 0; i < KNOWN_TAG_COUNT; i++) {
        if(contains_ci(raw, known_tags[i].raw_marker)) return known_tags[i].clean_name;
    }
    return NULL;
}

typedef struct {
    const char* probe;
    const char* match;
} FirmwareSignature;
static const FirmwareSignature signatures[] = {
    {"info", "bruce"},
    {"info", "fox esp32 firmware"},
};
#define SIGNATURE_COUNT (sizeof(signatures) / sizeof(signatures[0]))

static bool
    link_capture_response(Link* link, const char* probe, char* out, size_t out_capacity, uint32_t timeout_ms) {
    char command[40];
    snprintf(command, sizeof(command), "%.35s\r\n", probe);

    uint8_t discard;
    while(furi_stream_buffer_receive(link->rx_stream, &discard, 1, 0) > 0) {
    }

    furi_hal_serial_tx(link->serial, (const uint8_t*)command, strlen(command));

    out[0] = '\0';
    size_t len = 0;
    char line[LINE_MAX];
    uint32_t deadline = furi_get_tick() + timeout_ms;
    while(link_read_line_until(link, line, sizeof(line), deadline)) {
        size_t line_len = strlen(line);
        size_t sep = (len > 0) ? 1 : 0;
        if(len + sep + line_len >= out_capacity) break;
        if(sep) out[len++] = ' ';
        memcpy(out + len, line, line_len);
        len += line_len;
        out[len] = '\0';
    }
    return len > 0;
}

static bool link_listen_for_nmea(Link* link, char* out, size_t out_capacity, uint32_t timeout_ms) {
    char line[LINE_MAX];
    uint32_t deadline = furi_get_tick() + timeout_ms;
    if(link_read_line_until(link, line, sizeof(line), deadline) && line[0] == '$') {
        strncpy(out, line, out_capacity - 1);
        out[out_capacity - 1] = '\0';
        return true;
    }
    return false;
}

static bool link_check_bootloop_flood(Link* link, uint32_t window_ms, size_t min_bytes) {
    uint32_t deadline = furi_get_tick() + window_ms;
    size_t total = 0;
    while(furi_get_tick() < deadline) {
        if(link->cancel != NULL && *link->cancel) return false;
        uint8_t byte;
        if(furi_stream_buffer_receive(link->rx_stream, &byte, 1, 50) > 0) total++;
    }
    return total >= min_bytes;
}

static void post_progress(App* app, const char* status, size_t index, size_t total) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    strncpy(app->progress_status, status, sizeof(app->progress_status) - 1);
    app->progress_status[sizeof(app->progress_status) - 1] = '\0';
    app->progress_index = index;
    app->progress_total = total;
    furi_mutex_release(app->mutex);
    view_dispatcher_send_custom_event(app->view_dispatcher, DetectorEventProgress);
}

static void post_result(
    App* app,
    bool found,
    bool any_pin_claimed,
    const char* banner,
    size_t pin,
    uint32_t baud,
    bool flood_detected,
    size_t flood_pin) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->result_found = found;
    app->result_any_pin_claimed = any_pin_claimed;
    strncpy(app->result_banner, banner, sizeof(app->result_banner) - 1);
    app->result_banner[sizeof(app->result_banner) - 1] = '\0';
    app->result_pin = pin;
    app->result_baud = baud;
    app->result_flood_detected = flood_detected;
    app->result_flood_pin = flood_pin;
    app->scan_done = true;
    furi_mutex_release(app->mutex);
    view_dispatcher_send_custom_event(app->view_dispatcher, DetectorEventProgress);
}

static int32_t scan_worker(void* context) {
    App* app = context;
    size_t total_combos = PIN_OPTION_COUNT * BAUD_OPTION_COUNT;

    post_progress(app, "Starting...", 0, total_combos);

    char banner[BANNER_BUFFER_MAX];
    bool found = false;
    size_t found_pin = 0;
    uint32_t found_baud = 0;
    bool any_pin_claimed = false;
    bool flood_detected = false;
    size_t flood_pin = 0;

    for(size_t p = 0; p < PIN_OPTION_COUNT && !found && !flood_detected && !app->cancel_requested;
        p++) {
        Expansion* expansion = furi_record_open(RECORD_EXPANSION);
        expansion_disable(expansion);

        FuriHalSerialHandle* handle = furi_hal_serial_control_acquire(pin_options[p].serial_id);
        if(handle == NULL) {
            expansion_enable(expansion);
            furi_record_close(RECORD_EXPANSION);
            char status[64];
            snprintf(status, sizeof(status), "%.20s: in use", pin_options[p].label);
            post_progress(app, status, p * BAUD_OPTION_COUNT, total_combos);
            continue;
        }
        any_pin_claimed = true;

        bool owned = !furi_hal_bus_is_enabled(link_bus_for_serial(pin_options[p].serial_id));
        if(owned) furi_hal_serial_init(handle, baud_options[0]);

        Link link;
        link.rx_stream = furi_stream_buffer_alloc(512, 1);
        link.serial = handle;
        link.cancel = &app->cancel_requested;
        furi_hal_serial_async_rx_start(handle, link_rx_callback, &link, false);

        for(size_t b = 0; b < BAUD_OPTION_COUNT && !found && !flood_detected &&
                          !app->cancel_requested;
            b++) {
            size_t combo_index = p * BAUD_OPTION_COUNT + b;
            char status[64];
            snprintf(
                status,
                sizeof(status),
                "%.20s @ %lu",
                pin_options[p].label,
                (unsigned long)baud_options[b]);
            post_progress(app, status, combo_index, total_combos);

            furi_hal_serial_set_br(handle, baud_options[b]);

            uint8_t discard;
            while(furi_stream_buffer_receive(link.rx_stream, &discard, 1, 0) > 0) {
            }

            char capture[CAPTURE_MAX];
            if(link_listen_for_nmea(&link, capture, sizeof(capture), NMEA_LISTEN_MS)) {
                snprintf(banner, sizeof(banner), "GPS (NMEA): %.100s", capture);
                found = true;
                found_pin = p;
                found_baud = baud_options[b];
                break;
            }

            bool signature_found = false;
            for(size_t s = 0; s < SIGNATURE_COUNT; s++) {
                if(link_capture_response(
                       &link, signatures[s].probe, capture, sizeof(capture), FINGERPRINT_TIMEOUT_MS) &&
                   contains_ci(capture, signatures[s].match)) {
                    const char* clean = lookup_known_tag(capture);
                    if(clean != NULL) {
                        snprintf(banner, sizeof(banner), "%.100s", clean);
                    } else {
                        snprintf(banner, sizeof(banner), "Match \"%.20s\": %.100s", signatures[s].match, capture);
                    }
                    found = true;
                    signature_found = true;
                    found_pin = p;
                    found_baud = baud_options[b];
                    break;
                }
            }
            if(signature_found) break;

            furi_hal_serial_tx(handle, (const uint8_t*)"AT\r\n", 4);
            if(link_expect_ok(&link, AT_TIMEOUT_MS)) {
                furi_hal_serial_tx(handle, (const uint8_t*)"AT+GMR\r\n", 8);
                if(!link_get_firmware_banner(&link, banner, sizeof(banner), GMR_TIMEOUT_MS)) {
                    snprintf(banner, sizeof(banner), "ESP-AT (no version banner)");
                } else {
                    char with_prefix[LINE_MAX];
                    snprintf(with_prefix, sizeof(with_prefix), "ESP-AT: %.100s", banner);
                    strncpy(banner, with_prefix, sizeof(banner) - 1);
                    banner[sizeof(banner) - 1] = '\0';
                }
                found = true;
                found_pin = p;
                found_baud = baud_options[b];
                break;
            }

            char capture_repeat[CAPTURE_MAX];
            if(link_capture_response(&link, "?", capture, sizeof(capture), FINGERPRINT_TIMEOUT_MS) &&
               link_capture_response(
                   &link, "?", capture_repeat, sizeof(capture_repeat), FINGERPRINT_TIMEOUT_MS) &&
               strcmp(capture, capture_repeat) == 0) {
                const char* clean = lookup_known_tag(capture);
                if(clean != NULL) {
                    snprintf(banner, sizeof(banner), "%.100s", clean);
                } else {
                    snprintf(banner, sizeof(banner), "Unidentified: %.100s", capture);
                }
                found = true;
                found_pin = p;
                found_baud = baud_options[b];
                break;
            }

            if(baud_options[b] == 115200 &&
               link_check_bootloop_flood(&link, FLOOD_LISTEN_MS, FLOOD_MIN_BYTES)) {
                flood_detected = true;
                flood_pin = p;
                break;
            }
        }

        furi_hal_serial_async_rx_stop(handle);
        if(owned) furi_hal_serial_deinit(handle);
        furi_hal_serial_control_release(handle);
        expansion_enable(expansion);
        furi_record_close(RECORD_EXPANSION);
        furi_stream_buffer_free(link.rx_stream);
    }

    if(app->cancel_requested) return 0;
    post_result(
        app,
        found,
        any_pin_claimed,
        found ? banner : "",
        found_pin,
        found_baud,
        flood_detected,
        flood_pin);
    return 0;
}

static void start_scan(App* app) {
    if(app->scanning) return;
    app->scanning = true;
    app->scan_done = false;
    app->cancel_requested = false;

    if(app->scan_thread != NULL) {
        furi_thread_join(app->scan_thread);
        furi_thread_free(app->scan_thread);
    }
    app->scan_thread = furi_thread_alloc_ex("FoxDetectorScan", 2048, scan_worker, app);
    furi_thread_start(app->scan_thread);
}

static void scan_button_callback(GuiButtonType result, InputType type, void* context) {
    App* app = context;
    if(type == InputTypeShort && result == GuiButtonTypeCenter) start_scan(app);
}

static App* s_progress_view_app = NULL;

#define PROGRESS_TITLE_Y     2
#define PROGRESS_COUNTER_Y   16
#define PROGRESS_BAR_X       10
#define PROGRESS_BAR_Y       30
#define PROGRESS_BAR_W       108
#define PROGRESS_BAR_H       14
#define PROGRESS_BAR_RADIUS  4
#define PROGRESS_BAR_PAD     2
#define PROGRESS_STATUS_Y    52

static void progress_draw_cb(Canvas* canvas, void* model) {
    UNUSED(model);
    App* app = s_progress_view_app;
    if(app == NULL) return;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, PROGRESS_TITLE_Y, AlignCenter, AlignTop, "Scanning...");

    canvas_set_font(canvas, FontSecondary);
    char counter[24];
    snprintf(
        counter,
        sizeof(counter),
        "%u / %u",
        (unsigned)app->progress_display_index,
        (unsigned)app->progress_display_total);
    canvas_draw_str_aligned(canvas, 64, PROGRESS_COUNTER_Y, AlignCenter, AlignTop, counter);

    canvas_draw_rframe(
        canvas, PROGRESS_BAR_X, PROGRESS_BAR_Y, PROGRESS_BAR_W, PROGRESS_BAR_H, PROGRESS_BAR_RADIUS);

    size_t total = app->progress_display_total;
    size_t index = app->progress_display_index;
    if(index > total) index = total;

    int32_t inner_w = PROGRESS_BAR_W - PROGRESS_BAR_PAD * 2;
    int32_t inner_h = PROGRESS_BAR_H - PROGRESS_BAR_PAD * 2;
    int32_t fill_w = (total > 0) ? (int32_t)((size_t)inner_w * index / total) : 0;
    if(fill_w > inner_w) fill_w = inner_w;

    if(fill_w > 0) {
        int32_t fill_radius = PROGRESS_BAR_RADIUS - PROGRESS_BAR_PAD;
        if(fill_radius < 0) fill_radius = 0;
        int32_t max_radius = (fill_w < inner_h ? fill_w : inner_h) / 2;
        if(fill_radius > max_radius) fill_radius = max_radius;
        canvas_draw_rbox(
            canvas,
            PROGRESS_BAR_X + PROGRESS_BAR_PAD,
            PROGRESS_BAR_Y + PROGRESS_BAR_PAD,
            fill_w,
            inner_h,
            fill_radius);
    }

    canvas_draw_str_aligned(
        canvas, 64, PROGRESS_STATUS_Y, AlignCenter, AlignTop, app->progress_display_status);
}

static bool progress_input_cb(InputEvent* event, void* context) {
    UNUSED(event);
    UNUSED(context);

    return false;
}

#define RESULT_TABLE_X      1
#define RESULT_TABLE_W      126
#define RESULT_ROW_H        14
#define RESULT_TABLE_ROWS   3
#define RESULT_TABLE_Y      3
#define RESULT_TABLE_H      (RESULT_ROW_H * RESULT_TABLE_ROWS)
#define RESULT_TABLE_RADIUS 4
#define RESULT_LABEL_PAD    4
#define RESULT_VALUE_PAD    4

static App* s_result_view_app = NULL;

static size_t result_fit_chars(Canvas* canvas, const char* text, int32_t max_w) {
    size_t len = strlen(text);
    size_t n = 0;
    char buf[BANNER_BUFFER_MAX];
    while(n < len) {
        size_t take = n + 1;
        size_t cap = take < sizeof(buf) - 1 ? take : sizeof(buf) - 1;
        memcpy(buf, text, cap);
        buf[cap] = '\0';
        if((int32_t)canvas_string_width(canvas, buf) > max_w) break;
        n = take;
    }
    return n;
}

static void
    result_draw_value(Canvas* canvas, int32_t x, int32_t y, int32_t max_w, const char* value) {
    canvas_set_font(canvas, FontSecondary);
    if((int32_t)canvas_string_width(canvas, value) <= max_w) {
        canvas_draw_str_aligned(canvas, x, y, AlignLeft, AlignCenter, value);
        return;
    }
    int32_t dots_w = (int32_t)canvas_string_width(canvas, "...");
    size_t fit = result_fit_chars(canvas, value, max_w - dots_w);
    char buf[BANNER_BUFFER_MAX];
    size_t n = fit < sizeof(buf) - 4 ? fit : sizeof(buf) - 4;
    memcpy(buf, value, n);
    buf[n] = '.';
    buf[n + 1] = '.';
    buf[n + 2] = '.';
    buf[n + 3] = '\0';
    canvas_draw_str_aligned(canvas, x, y, AlignLeft, AlignCenter, buf);
}

static void result_table_draw_cb(Canvas* canvas, void* model) {
    UNUSED(model);
    App* app = s_result_view_app;
    if(app == NULL) return;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    static const char* labels[RESULT_TABLE_ROWS] = {"Pins", "Baud", "Info"};
    char baud_str[16];
    snprintf(baud_str, sizeof(baud_str), "%lu", (unsigned long)app->result_baud);
    const char* values[RESULT_TABLE_ROWS] = {
        pin_options[app->result_pin].label, baud_str, app->result_banner};

    int32_t label_w = 0;
    for(size_t i = 0; i < RESULT_TABLE_ROWS; i++) {
        int32_t w = (int32_t)canvas_string_width(canvas, labels[i]);
        if(w > label_w) label_w = w;
    }
    int32_t col1_w = label_w + RESULT_LABEL_PAD * 2;
    int32_t col2_w = RESULT_TABLE_W - col1_w;

    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(
        canvas, RESULT_TABLE_X, RESULT_TABLE_Y, RESULT_TABLE_W, RESULT_TABLE_H, RESULT_TABLE_RADIUS);
    canvas_draw_box(canvas, RESULT_TABLE_X + 1, RESULT_TABLE_Y + 1, col1_w - 1, RESULT_TABLE_H - 2);

    for(size_t i = 0; i < RESULT_TABLE_ROWS; i++) {
        int32_t row_y = RESULT_TABLE_Y + (int32_t)i * RESULT_ROW_H;
        int32_t cy = row_y + RESULT_ROW_H / 2;

        canvas_set_color(canvas, ColorWhite);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(
            canvas, RESULT_TABLE_X + RESULT_LABEL_PAD, cy, AlignLeft, AlignCenter, labels[i]);

        canvas_set_color(canvas, ColorBlack);
        int32_t value_x = RESULT_TABLE_X + col1_w + RESULT_VALUE_PAD;
        int32_t value_max_w = col2_w - RESULT_VALUE_PAD * 2;
        result_draw_value(canvas, value_x, cy, value_max_w, values[i]);
    }

    const char* label = "Rescan";
    const Icon* icon = &I_ButtonCenter_7x7;
    int32_t icon_gap = 3;
    int32_t pad = 10;
    int32_t btn_h = 14;
    int32_t btn_y = 64 - btn_h - 2;
    canvas_set_font(canvas, FontSecondary);
    int32_t group_w = icon->width + icon_gap + (int32_t)canvas_string_width(canvas, label);
    int32_t btn_w = group_w + pad * 2;
    int32_t btn_x = (128 - btn_w) / 2;

    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rbox(canvas, btn_x, btn_y, btn_w, btn_h, 3);
    canvas_set_color(canvas, ColorWhite);
    int32_t gx = btn_x + pad;
    canvas_draw_icon(canvas, gx, btn_y + (btn_h - icon->height) / 2, icon);
    canvas_draw_str_aligned(
        canvas, gx + icon->width + icon_gap, btn_y + btn_h / 2, AlignLeft, AlignCenter, label);
    canvas_set_color(canvas, ColorBlack);
}

static bool result_table_input_cb(InputEvent* event, void* context) {
    App* app = context;
    if(event->type != InputTypeShort) return false;
    switch(event->key) {
    case InputKeyOk:

        start_scan(app);
        return true;
    default:
        return false;
    }
}

static void render_result(
    App* app,
    bool any_pin_claimed,
    bool flood_detected,
    size_t flood_pin) {
    widget_reset(app->widget);

    char text[LINE_MAX + 64];
    if(flood_detected) {
        snprintf(
            text,
            sizeof(text),
            "ESP32 detected on\nPins: %.20s\nbut not flashed right -\ncontinuous unprompted\ndata (boot-loop).\nReflash firmware.",
            pin_options[flood_pin].label);
    } else if(any_pin_claimed) {
        snprintf(
            text,
            sizeof(text),
            "Nothing responded on\nany pin/baud combo.\nCheck wiring and power -\nsee README for what\nthis can and can't\nrecognize.");
    } else {
        snprintf(
            text,
            sizeof(text),
            "Could not claim any\nUART pins at all.\nAnother app may be\nusing them.");
    }

    widget_add_text_scroll_element(app->widget, 0, 0, 128, 47, text);
    widget_add_button_element(app->widget, GuiButtonTypeCenter, "Rescan", scan_button_callback, app);
}

static bool custom_event_callback(void* context, uint32_t event) {
    App* app = context;
    if(event != DetectorEventProgress) return false;

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    bool done = app->scan_done;
    size_t index = app->progress_index;
    size_t total = app->progress_total;
    char status[64];
    strncpy(status, app->progress_status, sizeof(status) - 1);
    status[sizeof(status) - 1] = '\0';
    bool found = app->result_found;
    bool any_pin_claimed = app->result_any_pin_claimed;
    bool flood_detected = app->result_flood_detected;
    size_t flood_pin = app->result_flood_pin;
    furi_mutex_release(app->mutex);

    if(!done) {
        app->progress_display_index = index;
        app->progress_display_total = total;
        strncpy(app->progress_display_status, status, sizeof(app->progress_display_status) - 1);
        app->progress_display_status[sizeof(app->progress_display_status) - 1] = '\0';
        view_dispatcher_switch_to_view(app->view_dispatcher, DetectorViewProgress);
        with_view_model(app->progress_view, uint8_t * _m, { UNUSED(_m); }, true);
    } else if(found) {
        view_dispatcher_switch_to_view(app->view_dispatcher, DetectorViewResultTable);
        app->scanning = false;
    } else {
        render_result(app, any_pin_claimed, flood_detected, flood_pin);
        view_dispatcher_switch_to_view(app->view_dispatcher, DetectorViewResult);
        app->scanning = false;
    }
    return true;
}

static bool navigation_callback(void* context) {
    App* app = context;
    if(app->scanning) app->cancel_requested = true;
    view_dispatcher_stop(app->view_dispatcher);
    return true;
}

static App* app_alloc(void) {
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, navigation_callback);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, custom_event_callback);

    app->widget = widget_alloc();

    widget_add_string_multiline_element(
        app->widget, 64, 4, AlignCenter, AlignTop, FontPrimary, "Fox ESP32\nDetector");
    widget_add_string_multiline_element(
        app->widget,
        64,
        26,
        AlignCenter,
        AlignTop,
        FontSecondary,
        "Checks what's on\nGPIO. Press Scan.");
    widget_add_button_element(app->widget, GuiButtonTypeCenter, "Scan", scan_button_callback, app);

    app->progress_view = view_alloc();
    view_set_draw_callback(app->progress_view, progress_draw_cb);
    view_set_input_callback(app->progress_view, progress_input_cb);
    view_set_context(app->progress_view, app);
    view_allocate_model(app->progress_view, ViewModelTypeLocking, sizeof(uint8_t));
    s_progress_view_app = app;

    app->result_view = view_alloc();
    view_set_draw_callback(app->result_view, result_table_draw_cb);
    view_set_input_callback(app->result_view, result_table_input_cb);
    view_set_context(app->result_view, app);
    view_allocate_model(app->result_view, ViewModelTypeLocking, sizeof(uint8_t));
    s_result_view_app = app;

    view_dispatcher_add_view(
        app->view_dispatcher, DetectorViewResult, widget_get_view(app->widget));
    view_dispatcher_add_view(app->view_dispatcher, DetectorViewProgress, app->progress_view);
    view_dispatcher_add_view(app->view_dispatcher, DetectorViewResultTable, app->result_view);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, DetectorViewResult);

    return app;
}

static void app_free(App* app) {
    if(app->scan_thread != NULL) {
        furi_thread_join(app->scan_thread);
        furi_thread_free(app->scan_thread);
    }

    view_dispatcher_remove_view(app->view_dispatcher, DetectorViewResult);
    view_dispatcher_remove_view(app->view_dispatcher, DetectorViewProgress);
    view_dispatcher_remove_view(app->view_dispatcher, DetectorViewResultTable);
    widget_free(app->widget);
    view_free(app->progress_view);
    view_free(app->result_view);
    s_progress_view_app = NULL;
    s_result_view_app = NULL;
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);

    furi_kernel_lock();
    furi_mutex_free(app->mutex);
    free(app);
    furi_kernel_unlock();
}

int32_t fox_esp32_detector_main(void* p) {
    UNUSED(p);
    App* app = app_alloc();
    view_dispatcher_run(app->view_dispatcher);
    app_free(app);
    return 0;
}
