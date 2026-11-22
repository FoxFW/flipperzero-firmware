// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 7h30th3r0n3. Part of Flipper-EvilRelay: https://github.com/7h30th3r0n3/Flipper-EvilRelay
#include "relay_uart.h"

#include <furi.h>
#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>
#include <expansion/expansion.h>

#define TAG "RelayUart"
#define F_MAGIC 0x5A

// Relay frame: [0x5A][type][seq][lenHi][lenLo][payload].
#define RELAY_HDR_LEN 5
// Receive-side cap: large enough to accept any normal card response without dropping
// it as "bogus length" and desynchronising the stream. The NFC side returns the
// response to the reader split across as many chained I-blocks as the reader's FSD
// needs (see relay_nfc.c), so this cap is just the per-response reassembly ceiling.
#define RELAY_PAYLOAD_MAX 512
#define RELAY_FRAME_MAX (RELAY_HDR_LEN + RELAY_PAYLOAD_MAX)
#define RX_STREAM_SIZE 1024
#define CONSOLE_LOG_BAUD 230400 // default Flipper console/log baud, restored on stop

static FuriHalSerialHandle* g_serial = NULL;
static Expansion* g_expansion = NULL;
static FuriStreamBuffer* g_rx_stream = NULL;
static FuriThread* g_worker = NULL;
static volatile bool g_running = false;
static RelayUartRxCallback g_cb = NULL;
static void* g_cb_ctx = NULL;
static volatile uint32_t g_rx_bytes = 0;
static volatile bool g_link_seen = false;

uint32_t relay_uart_rx_bytes(void) {
    return g_rx_bytes;
}
bool relay_uart_link_seen(void) {
    return g_link_seen;
}

// ISR context: drain the UART FIFO into the stream buffer. No heavy work here.
static void relay_uart_rx_isr(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* ctx) {
    UNUSED(ctx);
    if(event & FuriHalSerialRxEventData) {
        while(furi_hal_serial_async_rx_available(handle)) {
            uint8_t b = furi_hal_serial_async_rx(handle);
            furi_stream_buffer_send(g_rx_stream, &b, 1, 0);
        }
    }
}

// Worker thread: reassemble [0x5A] frames from the byte stream and dispatch them
// (in thread context, so the callback may allocate NFC objects and block).
static int32_t relay_uart_worker(void* ctx) {
    UNUSED(ctx);
    static uint8_t frame[RELAY_FRAME_MAX]; // off-stack: worker stack stays shallow
    size_t have = 0; // bytes accumulated into frame[]
    uint16_t need_payload = 0; // payload length once the header is parsed

    while(g_running) {
        uint8_t b;
        size_t got = furi_stream_buffer_receive(g_rx_stream, &b, 1, 50);
        if(got == 0) continue;
        g_rx_bytes++;
        g_link_seen = true;

        // Resynchronise on MAGIC while we have no header yet.
        if(have == 0) {
            if(b != F_MAGIC) continue;
            frame[have++] = b;
            continue;
        }
        frame[have++] = b;

        if(have == RELAY_HDR_LEN) {
            need_payload = ((uint16_t)frame[3] << 8) | frame[4];
            if(need_payload > RELAY_PAYLOAD_MAX) {
                // Bogus length: drop and resynchronise.
                have = 0;
                need_payload = 0;
            }
            continue;
        }
        if(have >= RELAY_HDR_LEN && have == (size_t)(RELAY_HDR_LEN + need_payload)) {
            if(g_cb) g_cb(frame[1], frame + RELAY_HDR_LEN, need_payload, g_cb_ctx);
            have = 0;
            need_payload = 0;
        }
    }
    return 0;
}

bool relay_uart_start(uint32_t baud, RelayUartRxCallback callback, void* context) {
    g_cb = callback;
    g_cb_ctx = context;
    g_rx_bytes = 0;
    g_link_seen = false;

    // The Expansion service owns the USART (GPIO 13/14) by default and must be
    // disabled BEFORE acquiring the serial handle, otherwise furi_hal_serial
    // furi_check-crashes. Re-enabled in relay_uart_stop() after release.
    FURI_LOG_I(TAG, "start: open expansion");
    g_expansion = furi_record_open(RECORD_EXPANSION);
    FURI_LOG_I(TAG, "start: expansion_disable");
    expansion_disable(g_expansion);

    // USART1 (GPIO 13/14) is also the system log/console port: FURI_LOG writes to
    // pin 13 by default, which collides with our relay use and crashes/hangs the
    // firmware the instant real traffic arrives. Move the console OFF the USART by
    // redirecting logging to the LPUART (pins 15/16, unused here). NOTE: passing
    // FuriHalSerialIdMax/baud 0 to "disable" logging hangs the firmware, so we
    // redirect to a valid port instead. Restored to USART in _stop().
    FURI_LOG_I(TAG, "start: move logging to LPUART");
    furi_hal_serial_control_set_logging_config(FuriHalSerialIdLpuart, CONSOLE_LOG_BAUD);
    FURI_LOG_I(TAG, "start: logging moved OK");

    /* (removed furi_hal_serial_control_is_busy pre-check: it is disabled in the FAP API,
     * and furi_hal_serial_control_acquire() below already returns NULL when the USART is
     * busy, handled identically by the if(!g_serial) cleanup path.) */
    g_serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(!g_serial) {
        FURI_LOG_E(TAG, "acquire USART failed");
        furi_hal_serial_control_set_logging_config(FuriHalSerialIdUsart, CONSOLE_LOG_BAUD);
        expansion_enable(g_expansion);
        furi_record_close(RECORD_EXPANSION);
        g_expansion = NULL;
        return false;
    }
    furi_hal_serial_init(g_serial, baud);

    g_rx_stream = furi_stream_buffer_alloc(RX_STREAM_SIZE, 1);
    g_running = true;
    // Generous stack: the RX callback starts the NFC listener (set_uid, ATS fill,
    // nfc_listener_alloc/start) from this thread, deep call chains that overflow a
    // small stack and trip the stack-canary furi_check (freeze on first IDENT).
    g_worker = furi_thread_alloc_ex("RelayUartRx", 16384, relay_uart_worker, NULL);
    furi_thread_start(g_worker);

    furi_hal_serial_async_rx_start(g_serial, relay_uart_rx_isr, NULL, false);
    FURI_LOG_I(TAG, "UART up @ %lu baud (USART 13/14)", (unsigned long)baud);
    return true;
}

void relay_uart_stop(void) {
    if(g_serial) {
        furi_hal_serial_async_rx_stop(g_serial);
    }
    g_running = false;
    if(g_worker) {
        furi_thread_join(g_worker);
        furi_thread_free(g_worker);
        g_worker = NULL;
    }
    if(g_serial) {
        furi_hal_serial_deinit(g_serial);
        furi_hal_serial_control_release(g_serial);
        g_serial = NULL;
    }
    // Restore system logging to the USART before handing the port back.
    furi_hal_serial_control_set_logging_config(FuriHalSerialIdUsart, CONSOLE_LOG_BAUD);
    if(g_expansion) {
        expansion_enable(g_expansion);
        furi_record_close(RECORD_EXPANSION);
        g_expansion = NULL;
    }
    if(g_rx_stream) {
        furi_stream_buffer_free(g_rx_stream);
        g_rx_stream = NULL;
    }
    g_cb = NULL;
    g_cb_ctx = NULL;
}

void relay_uart_tx_frame(uint8_t type, uint8_t seq, const uint8_t* data, uint16_t len) {
    if(!g_serial) return;
    if(len > RELAY_PAYLOAD_MAX) len = RELAY_PAYLOAD_MAX;
    uint8_t f[RELAY_FRAME_MAX];
    f[0] = F_MAGIC;
    f[1] = type;
    f[2] = seq;
    f[3] = (len >> 8) & 0xFF;
    f[4] = len & 0xFF;
    if(len) memcpy(f + RELAY_HDR_LEN, data, len);
    furi_hal_serial_tx(g_serial, f, RELAY_HDR_LEN + len);
}
