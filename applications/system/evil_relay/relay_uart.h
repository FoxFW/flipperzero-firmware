// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 7h30th3r0n3. Part of Flipper-EvilRelay: https://github.com/7h30th3r0n3/Flipper-EvilRelay
#pragma once
// UART transport for the relay: a clean byte pipe to the companion ESP32 on the
// Flipper's USART pins (13 = TX, 14 = RX). The ESP32 bridges this UART to the
// air side (phone over Wi-Fi/TCP, or an Evil reader unit over ESP-NOW), carrying
// the SAME relay frame protocol end to end:
//   [0x5A][type][seq][lenHi][lenLo][payload]
//     type 1 IDENT  reader->Flipper : [uidLen][uid..][atqa0][atqa1][sak][atsLen][ats..]
//     type 2 REQ    Flipper->reader : APDU from the terminal (TPE)
//     type 3 RESP   reader->Flipper : card response
//
// The async RX runs in interrupt context, so this module only buffers bytes
// there (into a stream buffer) and reassembles complete frames on a worker
// thread, invoking the callback in THREAD context (safe for NFC allocations and
// blocking waits).
#include <stdint.h>
#include <stdbool.h>

// Invoked in worker-thread context once a full relay frame has been reassembled.
typedef void (*RelayUartRxCallback)(uint8_t type, const uint8_t* data, uint16_t len, void* context);

// Acquire the USART, start RX reassembly. Returns false if the bus is busy.
bool relay_uart_start(uint32_t baud, RelayUartRxCallback callback, void* context);
void relay_uart_stop(void);

// Build and transmit one relay frame to the ESP32. Safe to call from the NFC
// worker thread (semi-blocking, bounded by the UART pipe).
void relay_uart_tx_frame(uint8_t type, uint8_t seq, const uint8_t* data, uint16_t len);

// Diagnostics for the UI.
uint32_t relay_uart_rx_bytes(void);
bool relay_uart_link_seen(void);
