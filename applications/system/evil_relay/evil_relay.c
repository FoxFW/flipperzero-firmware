// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 7h30th3r0n3. Part of Flipper-EvilRelay: https://github.com/7h30th3r0n3/Flipper-EvilRelay
// EvilRelay (Flipper): ISO-DEP emulator half of the NFC relay.
// The Flipper emulates an ISO14443-4 card to the terminal (TPE) and relays the
// application APDUs over its USART (pins 13=TX / 14=RX) to a companion ESP32.
// The ESP32 bridges that UART to the air side (a phone over Wi-Fi/TCP, or an
// Evil reader unit over ESP-NOW), which reads the real card and returns the
// responses. Same relay frame protocol ([0x5A]...) runs end to end.
#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include "relay_uart.h"
#include "relay_nfc.h"

#define TAG "EvilRelay"

// UART link baud to the ESP32. Must match the ESP32 sketch (RELAY_UART_BAUD).
#define RELAY_UART_BAUD 115200

typedef struct {
    volatile bool started;
} RelayState;

static RelayState* g_st;

static void relay_draw(Canvas* c, void* ctx) {
    RelayState* st = (RelayState*)ctx;
    canvas_clear(c);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 2, 12, "EvilRelay ISO-DEP");
    canvas_set_font(c, FontSecondary);
    char b[44];
    if(!st->started) {
        canvas_draw_str(c, 2, 26, "UART START FAILED");
        canvas_draw_str(c, 2, 38, "ESP32 on 13/14?");
    } else if(!relay_nfc_emulating()) {
        canvas_draw_str(c, 2, 26, relay_uart_link_seen() ? "Reader linked (UART)" : "Waiting reader (UART)");
        canvas_draw_str(c, 2, 38, "then present Flipper to TPE");
    } else {
        canvas_draw_str(c, 2, 26, "EMULATING - relaying");
        snprintf(b, sizeof(b), "APDUs relayed: %lu", (unsigned long)relay_nfc_apdu_count());
        canvas_draw_str(c, 2, 38, b);
    }
    snprintf(b, sizeof(b), "uart rx %lu", (unsigned long)relay_uart_rx_bytes());
    canvas_draw_str(c, 2, 52, b);
    canvas_draw_str(c, 2, 62, "Back=exit");
}

static void relay_input(InputEvent* e, void* ctx) {
    FuriMessageQueue* q = (FuriMessageQueue*)ctx;
    furi_message_queue_put(q, e, FuriWaitForever);
}

int32_t evil_relay_app(void* p) {
    UNUSED(p);
    g_st = malloc(sizeof(RelayState));
    memset(g_st, 0, sizeof(RelayState));
    FuriMessageQueue* queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    ViewPort* vp = view_port_alloc();
    view_port_draw_callback_set(vp, relay_draw, g_st);
    view_port_input_callback_set(vp, relay_input, queue);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, vp, GuiLayerFullscreen);

    // Bring up the NFC emulator first (it waits for the IDENT), then the UART
    // transport that will feed it frames from the ESP32.
    relay_nfc_start();
    g_st->started = relay_uart_start(RELAY_UART_BAUD, relay_nfc_on_frame, NULL);
    if(g_st->started) {
        FURI_LOG_I(TAG, "relay up: UART @ %d, NFC ready", RELAY_UART_BAUD);
    } else {
        FURI_LOG_E(TAG, "relay_uart_start FAILED");
    }

    InputEvent e;
    bool run = true; // stay on screen even if the UART failed, so we can see why
    uint32_t tick = 0;
    while(run) {
        if(furi_message_queue_get(queue, &e, 100) == FuriStatusOk) {
            if(e.type == InputTypeShort && e.key == InputKeyBack) run = false;
        }
        if(++tick % 20 == 0) {
            FURI_LOG_I(
                TAG,
                "alive started=%d rx=%lu apdu=%lu",
                g_st->started,
                (unsigned long)relay_uart_rx_bytes(),
                (unsigned long)relay_nfc_apdu_count());
        }
        view_port_update(vp);
    }

    relay_uart_stop();
    relay_nfc_stop();

    gui_remove_view_port(gui, vp);
    furi_record_close(RECORD_GUI);
    view_port_free(vp);
    furi_message_queue_free(queue);
    free(g_st);
    return 0;
}
