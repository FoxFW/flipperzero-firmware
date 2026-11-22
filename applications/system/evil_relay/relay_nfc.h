// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 7h30th3r0n3. Part of Flipper-EvilRelay: https://github.com/7h30th3r0n3/Flipper-EvilRelay
#pragma once
// NFC emulator half of the relay: the Flipper emulates an ISO14443-4 (ISO-DEP)
// card. The firmware answers RATS/PPS itself (ATS forged from the IDENT) and
// handles I-block/R-block chaining; only assembled application APDUs are relayed
// to the reader over UART (via relay_uart), which replays them on the real card
// and returns the response.
//
// Relay frame over UART: [0x5A][type][seq][lenHi][lenLo][payload]
//   type 1 IDENT  reader->Flipper : [uidLen][uid..][atqa0][atqa1][sak][atsLen][ats..]
//   type 2 REQ    Flipper->reader : application APDU from the terminal
//   type 3 RESP   reader->Flipper : card response
#include <stdint.h>
#include <stdbool.h>

void relay_nfc_start(void);
void relay_nfc_stop(void);

// RelayUartRxCallback: a decoded relay frame (IDENT or RESP) from the ESP32.
void relay_nfc_on_frame(uint8_t type, const uint8_t* data, uint16_t len, void* ctx);

bool relay_nfc_emulating(void);
uint32_t relay_nfc_apdu_count(void);
