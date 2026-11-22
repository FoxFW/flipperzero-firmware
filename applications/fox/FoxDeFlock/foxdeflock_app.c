

#include "foxdeflock_app.h"
#include "foxdeflock_scan.h"

#include <gui/elements.h>
#include <stdio.h>
#include <string.h>

#define FOXDEFLOCK_BAUD 115200
#define TICK_HZ         10
#define ESP32_CHECK_TIMEOUT_MS 1500

typedef struct {
    FuriHalSerialId serial_id;
    const char* label;
} PinOption;

static const PinOption PIN_OPTIONS[] = {
    {FuriHalSerialIdUsart, "13/14 (USART)"},
    {FuriHalSerialIdLpuart, "15/16 (LPUART)"},
};
#define PIN_OPTION_COUNT (sizeof(PIN_OPTIONS) / sizeof(PIN_OPTIONS[0]))

static const char* confidence_code(FlockConfidence c) {
    switch(c) {
    case FlockConfidenceConfirmed:
        return "CONF";
    case FlockConfidenceProbeFp:
        return "FP?";
    case FlockConfidenceLikely:
        return "LIKE";
    case FlockConfidencePossible:
        return "POSS";
    default:
        return "?";
    }
}

static const char* mode_label(FoxDeFlockScanMode m) {
    switch(m) {
    case FoxDeFlockModeWifiProbe:
        return "WiFi:Probe";
    case FoxDeFlockModeWifiBeacon:
        return "WiFi:Beacon";
    case FoxDeFlockModeBleFlock:
        return "BLE:Flock";
    default:
        return "?";
    }
}

static void draw_centered_str(Canvas* c, int y, const char* str) {
    int w = canvas_string_width(c, str);
    canvas_draw_str(c, (128 - w) / 2, y, str);
}

static void draw_esp32_check(Canvas* c, FoxDeFlockApp* app) {
    UNUSED(app);
    canvas_set_font(c, FontPrimary);
    draw_centered_str(c, 24, "Detecting ESP32...");

    canvas_set_font(c, FontSecondary);
    char dots[5] = "    ";
    uint8_t d = (uint8_t)((furi_get_tick() / 250) % 4);
    for(uint8_t i = 0; i < d; i++) dots[i] = '.';
    draw_centered_str(c, 38, dots);
}

#define FOXDEFLOCK_BUTTON_H 13
#define FOXDEFLOCK_BUTTON_PAD_X 3
#define FOXDEFLOCK_BUTTON_GAP 6
#define FOXDEFLOCK_BUTTON_R 3

static void draw_two_buttons(Canvas* c, bool left_focused, const char* left, const char* right) {
    int y = 48, h = FOXDEFLOCK_BUTTON_H;
    int lw = canvas_string_width(c, left) + FOXDEFLOCK_BUTTON_PAD_X * 2;
    int rw = canvas_string_width(c, right) + FOXDEFLOCK_BUTTON_PAD_X * 2;
    int gap = FOXDEFLOCK_BUTTON_GAP;
    int total = lw + gap + rw;
    int lx = (128 - total) / 2;
    int rx = lx + lw + gap;

    canvas_set_font(c, FontSecondary);

    if(left_focused) {
        canvas_draw_rbox(c, lx, y, lw, h, FOXDEFLOCK_BUTTON_R);
        canvas_set_color(c, ColorWhite);
        canvas_draw_str_aligned(c, lx + lw / 2, y + h / 2, AlignCenter, AlignCenter, left);
        canvas_set_color(c, ColorBlack);
    } else {
        canvas_draw_rframe(c, lx, y, lw, h, FOXDEFLOCK_BUTTON_R);
        canvas_draw_str_aligned(c, lx + lw / 2, y + h / 2, AlignCenter, AlignCenter, left);
    }

    if(!left_focused) {
        canvas_draw_rbox(c, rx, y, rw, h, FOXDEFLOCK_BUTTON_R);
        canvas_set_color(c, ColorWhite);
        canvas_draw_str_aligned(c, rx + rw / 2, y + h / 2, AlignCenter, AlignCenter, right);
        canvas_set_color(c, ColorBlack);
    } else {
        canvas_draw_rframe(c, rx, y, rw, h, FOXDEFLOCK_BUTTON_R);
        canvas_draw_str_aligned(c, rx + rw / 2, y + h / 2, AlignCenter, AlignCenter, right);
    }
}

static void draw_esp32_not_found(Canvas* c, FoxDeFlockApp* app) {
    canvas_set_font(c, FontPrimary);
    draw_centered_str(c, 8, "ESP32 not found");

    canvas_set_font(c, FontSecondary);
    draw_centered_str(c, 22, "Fox ESP32 Firmware");
    draw_centered_str(c, 32, "required, connected via GPIO.");

    draw_two_buttons(c, app->esp32_check_focus_settings, "Settings", "Retry");
}

static void draw_connect_settings(Canvas* c, FoxDeFlockApp* app) {
    canvas_set_font(c, FontPrimary);
    draw_centered_str(c, 10, "Connection");
    canvas_draw_line(c, 0, 13, 128, 13);

    canvas_set_font(c, FontSecondary);
    char line[32];
    snprintf(line, sizeof(line), "\x11 Pins: %s \x10", PIN_OPTIONS[app->pin_option_index].label);
    draw_centered_str(c, 32, line);

    draw_centered_str(c, 62, "[OK] Retry   [Back] Return");
}

static void draw_hit_row(Canvas* c, int y, const FoxDeFlockHit* h, bool selected) {
    if(selected) {
        canvas_draw_box(c, 0, y, 128, 10);
        canvas_set_color(c, ColorWhite);
    }
    char mac_tail[6];
    snprintf(mac_tail, sizeof(mac_tail), "%02X%02X", h->mac[4], h->mac[5]);

    const char* label = h->have_ssid ? h->ssid : (h->ble_name[0] ? h->ble_name : "(no name)");
    char row[40];
    snprintf(
        row,
        sizeof(row),
        "%s ..%s %.14s %s",
        h->source == FoxDeFlockSourceBle ? "B" : "W",
        mac_tail,
        label,
        confidence_code(h->confidence));
    canvas_draw_str(c, 2, y + 8, row);
    if(selected) canvas_set_color(c, ColorBlack);
}

static void draw_scanning(Canvas* c, FoxDeFlockApp* app) {
    canvas_set_font(c, FontSecondary);
    char header[40];
    snprintf(
        header, sizeof(header), "DeFlock %d hit%s", app->hit_count, app->hit_count == 1 ? "" : "s");
    canvas_draw_str(c, 2, 8, header);
    canvas_draw_str_aligned(c, 126, 8, AlignRight, AlignBottom, mode_label(app->scan_mode));
    canvas_draw_line(c, 0, 10, 128, 10);

    if(app->hit_count == 0) {
        canvas_draw_str_aligned(c, 64, 32, AlignCenter, AlignTop, "Scanning...");
        return;
    }

    const int row_h = 10;
    const int rows_visible = 5;
    if(app->selected_hit < app->list_offset) app->list_offset = app->selected_hit;
    if(app->selected_hit >= app->list_offset + rows_visible) {
        app->list_offset = app->selected_hit - rows_visible + 1;
    }
    for(int i = 0; i < rows_visible; i++) {
        uint16_t idx = app->list_offset + i;
        if(idx >= app->hit_count) break;
        draw_hit_row(c, 12 + i * row_h, &app->hits[idx], idx == app->selected_hit);
    }
}

static void draw_detail(Canvas* c, FoxDeFlockApp* app) {
    if(app->selected_hit >= app->hit_count) return;
    const FoxDeFlockHit* h = &app->hits[app->selected_hit];

    canvas_set_font(c, FontPrimary);
    char mac_str[18];
    snprintf(
        mac_str,
        sizeof(mac_str),
        "%02X:%02X:%02X:%02X:%02X:%02X",
        h->mac[0],
        h->mac[1],
        h->mac[2],
        h->mac[3],
        h->mac[4],
        h->mac[5]);
    canvas_draw_str_aligned(c, 64, 2, AlignCenter, AlignTop, mac_str);

    canvas_set_font(c, FontSecondary);
    char line[64];
    snprintf(
        line,
        sizeof(line),
        "Confidence: %s (%s)",
        flock_confidence_str(h->confidence),
        confidence_code(h->confidence));
    canvas_draw_str(c, 2, 22, line);

    snprintf(
        line,
        sizeof(line),
        "Class: %s   Src: %s",
        flock_class_str(h->dev_class),
        h->source == FoxDeFlockSourceBle ? "BLE" : "WiFi");
    canvas_draw_str(c, 2, 32, line);

    if(h->source == FoxDeFlockSourceWifi) {
        snprintf(
            line,
            sizeof(line),
            "%s  ch:%u  rssi:%d",
            h->have_ssid ? h->ssid : "(hidden/no ssid)",
            h->channel,
            h->rssi);
    } else {
        snprintf(
            line, sizeof(line), "%s  rssi:%d", h->ble_name[0] ? h->ble_name : "(no name)", h->rssi);
    }
    canvas_draw_str(c, 2, 42, line);

    snprintf(line, sizeof(line), "Sightings: %u", h->sightings);
    canvas_draw_str(c, 2, 52, line);

    elements_button_left(c, "Back");
}

static void draw_about(Canvas* c, FoxDeFlockApp* app) {
    UNUSED(app);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str_aligned(c, 64, 2, AlignCenter, AlignTop, "FoxDeFlock");
    canvas_set_font(c, FontSecondary);
    canvas_draw_str_aligned(c, 64, 16, AlignCenter, AlignTop, "Flock/ALPR camera detector");
    canvas_draw_str_aligned(c, 64, 26, AlignCenter, AlignTop, "Based on FlipDeFlock by");
    canvas_draw_str_aligned(c, 64, 36, AlignCenter, AlignTop, "ReconGrunt (GPL-3.0)");
    canvas_draw_str_aligned(c, 64, 48, AlignCenter, AlignTop, "Requires Fox ESP32 FW.");
    elements_button_left(c, "Back");
}

static void draw_cb(Canvas* c, void* ctx) {
    FoxDeFlockApp* app = ctx;
    canvas_clear(c);
    canvas_set_color(c, ColorBlack);
    switch(app->state) {
    case FoxDeFlockStateEsp32Check:
        draw_esp32_check(c, app);
        break;
    case FoxDeFlockStateEsp32NotFound:
        draw_esp32_not_found(c, app);
        break;
    case FoxDeFlockStateConnectSettings:
        draw_connect_settings(c, app);
        break;
    case FoxDeFlockStateScanning:
    case FoxDeFlockStatePaused:
        draw_scanning(c, app);
        break;
    case FoxDeFlockStateDetail:
        draw_detail(c, app);
        break;
    case FoxDeFlockStateAbout:
        draw_about(c, app);
        break;
    }
}

static void start_esp32_check(FoxDeFlockApp* app) {
    if(app->esp_at) {
        esp_at_free(app->esp_at);
        app->esp_at = NULL;
    }
    app->esp_at = esp_at_alloc(app->serial_id, FOXDEFLOCK_BAUD);
    app->esp32_probe_ok = false;
    app->esp32_check_start_tick = furi_get_tick();
    app->state = FoxDeFlockStateEsp32Check;
    foxdeflock_send_probe(app);
}

static void start_scanning(FoxDeFlockApp* app) {
    app->state = FoxDeFlockStateScanning;
    app->scan_mode = FoxDeFlockModeBleFlock;
    app->mode_started_tick = 0;
}

static void handle_input(FoxDeFlockApp* app, InputKey key, InputType type) {
    if(type != InputTypeShort && type != InputTypeLong) return;

    switch(app->state) {
    case FoxDeFlockStateEsp32Check:
        break;

    case FoxDeFlockStateEsp32NotFound:
        if((key == InputKeyLeft || key == InputKeyRight) && type == InputTypeShort) {
            app->esp32_check_focus_settings = !app->esp32_check_focus_settings;
        } else if(key == InputKeyOk && type == InputTypeShort) {
            if(app->esp32_check_focus_settings) {
                app->state = FoxDeFlockStateConnectSettings;
            } else {
                app->esp32_probe_tried_alt = false;
                start_esp32_check(app);
            }
        }
        break;

    case FoxDeFlockStateConnectSettings:
        if((key == InputKeyLeft || key == InputKeyRight) && type == InputTypeShort) {
            app->pin_option_index = (app->pin_option_index + 1) % PIN_OPTION_COUNT;
            app->serial_id = PIN_OPTIONS[app->pin_option_index].serial_id;
            GpioRemapSettings remap = {
                .esp32_uart_channel = (uint8_t)app->pin_option_index,
            };
            gpio_remap_settings_save(&remap);
        } else if(key == InputKeyOk && type == InputTypeShort) {
            app->esp32_probe_tried_alt = false;
            start_esp32_check(app);
        } else if(key == InputKeyBack && type == InputTypeShort) {
            app->state = FoxDeFlockStateEsp32NotFound;
        }
        break;

    case FoxDeFlockStateScanning:
    case FoxDeFlockStatePaused:
        if(key == InputKeyUp && type == InputTypeShort) {
            if(app->selected_hit > 0) app->selected_hit--;
        } else if(key == InputKeyDown && type == InputTypeShort) {
            if(app->selected_hit + 1 < app->hit_count) app->selected_hit++;
        } else if(key == InputKeyOk && type == InputTypeShort) {
            if(app->hit_count > 0) app->state = FoxDeFlockStateDetail;
        } else if(key == InputKeyOk && type == InputTypeLong) {
            app->state = FoxDeFlockStateAbout;
        }
        break;

    case FoxDeFlockStateDetail:
    case FoxDeFlockStateAbout:
        if(key == InputKeyBack && type == InputTypeShort) {
            app->state = FoxDeFlockStateScanning;
        }
        break;
    }
}

static void input_cb(InputEvent* event, void* ctx) {
    FoxDeFlockApp* app = ctx;
    furi_message_queue_put(app->event_queue, event, 0);
}

static void timer_cb(void* ctx) {
    FoxDeFlockApp* app = ctx;

    if(app->state == FoxDeFlockStateEsp32Check) {
        foxdeflock_check_drain(app);
        if(app->esp32_probe_ok) {
            start_scanning(app);
        } else if(furi_get_tick() - app->esp32_check_start_tick > furi_ms_to_ticks(ESP32_CHECK_TIMEOUT_MS)) {
            if(!app->esp32_probe_tried_alt) {

                app->esp32_probe_tried_alt = true;
                app->pin_option_index = (app->pin_option_index + 1) % PIN_OPTION_COUNT;
                app->serial_id = PIN_OPTIONS[app->pin_option_index].serial_id;
                GpioRemapSettings remap = {
                    .esp32_uart_channel = (uint8_t)app->pin_option_index,
                };
                gpio_remap_settings_save(&remap);
                start_esp32_check(app);
            } else {
                app->state = FoxDeFlockStateEsp32NotFound;
                app->esp32_check_focus_settings = false;
            }
        }
    } else if(app->state == FoxDeFlockStateScanning) {
        foxdeflock_scan_drain(app);
        foxdeflock_scan_pump(app);
    }

    view_port_update(app->view_port);
}

static FoxDeFlockApp* foxdeflock_app_alloc(void) {
    FoxDeFlockApp* app = malloc(sizeof(FoxDeFlockApp));
    memset(app, 0, sizeof(*app));

    GpioRemapSettings remap;
    gpio_remap_settings_load(&remap);
    app->pin_option_index = (remap.esp32_uart_channel < PIN_OPTION_COUNT) ?
                                 remap.esp32_uart_channel :
                                 0;
    app->serial_id = PIN_OPTIONS[app->pin_option_index].serial_id;
    app->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_cb, app);
    view_port_input_callback_set(app->view_port, input_cb, app);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    furi_hal_power_insomnia_enter();

    start_esp32_check(app);

    return app;
}

static void foxdeflock_app_free(FoxDeFlockApp* app) {
    furi_hal_power_insomnia_exit();
    if(app->esp_at) esp_at_free(app->esp_at);

    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(app->event_queue);
    free(app);
}

int32_t foxdeflock_app(void* p) {
    UNUSED(p);
    FoxDeFlockApp* app = foxdeflock_app_alloc();

    FuriTimer* timer = furi_timer_alloc(timer_cb, FuriTimerTypePeriodic, app);
    furi_timer_start(timer, furi_kernel_get_tick_frequency() / TICK_HZ);

    InputEvent event;
    bool running = true;
    while(running) {
        if(furi_message_queue_get(app->event_queue, &event, 10) == FuriStatusOk) {
            if(event.key == InputKeyBack && event.type == InputTypeLong &&
               app->state != FoxDeFlockStateDetail && app->state != FoxDeFlockStateAbout &&
               app->state != FoxDeFlockStateConnectSettings) {
                running = false;
            } else {
                handle_input(app, event.key, event.type);
            }
        }
    }

    furi_timer_stop(timer);
    furi_timer_free(timer);
    foxdeflock_app_free(app);
    return 0;
}
