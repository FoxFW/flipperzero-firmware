#include "pandora_esp32_cc1101.h"
#include "pandora_esp32_protocol.h"

#include <furi_hal_subghz.h>
#include <string.h>

#define TAG "PandoraEsp32CC"

/* TX ring: a keyfob frame is a few hundred edges; 1024 is ample. The async_tx
 * callback drains it one edge at a time (level_duration). */
#define TX_RING_LEN 1024u

/* Which CC1101 modulation preset is currently loaded (PandoraARM CcPreset). */
typedef enum {
    CcPresetOok = 0, /* OOK/ASK 650kHz BW (Pandora common / OOK remotes) */
    CcPreset2Fsk, /* 2-FSK (Pandora FSK/GFSK firmwares) */
} CcPreset;

typedef struct {
    bool level;
    uint32_t duration;
} RfEdge;

struct PandoraEsp32Cc1101 {
    PandoraEsp32RxEdgeCb rx_cb;
    void* rx_ctx;

    uint32_t frequency; /* current carrier (Hz) */
    uint32_t tuned_frequency; /* frequency currently programmed in the CC1101 */
    bool radio_on;
    CcPreset cc_preset; /* which custom preset is loaded right now */
    uint8_t rf_mode; /* PROTO_RF_* mode we are servicing */

    bool rx_running;
    bool tx_active;

    /* TX ring filled by pandora_esp32_cc1101_tx_edges, drained by radio_tx_cb. */
    RfEdge tx_ring[TX_RING_LEN];
    volatile uint32_t tx_head;
    volatile uint32_t tx_tail;
};

/* ---- CC1101 presets: copied VERBATIM from PandoraARM main.c ----------------*/

/* CC1101 OOK/ASK preset (650kHz BW, async serial). Register pairs {addr,val}
 * followed by the 8-byte PATABLE. Identical to the Flipper's
 * subghz_device_cc1101_preset_ook_650khz_async_regs. */
static const uint8_t PRESET_OOK650[] = {
    0x02, 0x0D, 0x07, 0x04, 0x08, 0x32, 0x0B, 0x06, 0x10, 0xF8, 0x11, 0x32, 0x12, 0x30,
    0x14, 0x00, 0x15, 0x00, 0x18, 0x18, 0x19, 0x16, 0x1B, 0x07, 0x1C, 0x00, 0x1D, 0x91,
    0x20, 0xFB, 0x21, 0xB6, 0x22, 0x11, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
};

/* CC1101 2-FSK preset (async serial). Derived from the Flipper's
 * subghz_device_cc1101_preset_2fsk_dev47_6khz_async_regs. */
static const uint8_t PRESET_2FSK[] = {
    0x02, 0x0D, 0x0B, 0x06, 0x08, 0x32, 0x07, 0x04, 0x14, 0x00, 0x13, 0x02, 0x12, 0x04,
    0x11, 0x83, 0x10, 0x67, 0x15, 0x47, 0x18, 0x18, 0x19, 0x16, 0x1D, 0x91, 0x1C, 0x00,
    0x1B, 0x07, 0x20, 0xFB, 0x22, 0x10, 0x21, 0x56, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
};

/* Clamp to a CC1101 band the Flipper supports (300-348, 387-464, 779-928 MHz).
 * Copied from PandoraARM cc1101_band_ok. */
static bool cc1101_band_ok(uint32_t hz) {
    return (hz >= 300000000u && hz <= 348000000u) ||
           (hz >= 387000000u && hz <= 464000000u) ||
           (hz >= 779000000u && hz <= 928000000u);
}

/* PROTO_MOD_* -> is this a 2-FSK preset? (PandoraARM cc_preset_for_modtyp). */
static bool cc_preset_is_fsk(uint8_t modulation) {
    return (modulation == PROTO_MOD_FSK || modulation == PROTO_MOD_GFSK);
}

/* subghz RX IRQ: hand the edge to the caller (forwarded to ESP32 as RX_EDGE).
 * Copied shape from PandoraARM radio_rx_callback (do NOT block). */
static void pandora_esp32_rx_callback(bool level, uint32_t duration, void* context) {
    PandoraEsp32Cc1101* cc = (PandoraEsp32Cc1101*)context;
    if(cc->rx_cb) cc->rx_cb(level, duration, cc->rx_ctx);
}

/* async_tx callback: drains the ring of edges. Copied from PandoraARM
 * radio_tx_callback. */
static LevelDuration pandora_esp32_tx_callback(void* context) {
    PandoraEsp32Cc1101* cc = (PandoraEsp32Cc1101*)context;
    uint32_t tail = cc->tx_tail;
    if(tail == cc->tx_head) return level_duration_reset();
    RfEdge e = cc->tx_ring[tail];
    cc->tx_tail = (tail + 1) % TX_RING_LEN;
    return level_duration_make(e.level, e.duration);
}

/* Load (or reload) the custom preset that matches `modulation`, on an idle
 * radio. Mirrors PandoraARM rf_reapply_preset but driven by the ESP32's
 * reported modulation instead of the local Si4432 regs. */
static void cc_apply_preset(PandoraEsp32Cc1101* cc, uint8_t modulation) {
    if(!cc->radio_on) return;
    CcPreset want = cc_preset_is_fsk(modulation) ? CcPreset2Fsk : CcPresetOok;
    if(want == cc->cc_preset && cc->frequency == cc->tuned_frequency) {
        /* RF_MODE is emitted repeatedly while firmware runs. Avoid stopping
         * async RX and retuning the CC1101 for an unchanged configuration. */
        return;
    }
    if(cc->tx_active) {
        furi_hal_subghz_stop_async_tx();
        cc->tx_active = false;
    }
    if(cc->rx_running) {
        furi_hal_subghz_stop_async_rx();
        cc->rx_running = false;
    }
    furi_hal_subghz_idle();
    if(want != cc->cc_preset) {
        furi_hal_subghz_load_custom_preset(want == CcPreset2Fsk ? PRESET_2FSK : PRESET_OOK650);
        cc->cc_preset = want;
    }
    furi_hal_subghz_set_frequency_and_path(cc->frequency);
    cc->tuned_frequency = cc->frequency;
    furi_hal_subghz_idle();
    FURI_LOG_I(
        TAG, "preset %s @%lu Hz", want == CcPreset2Fsk ? "2FSK" : "OOK650",
        (unsigned long)cc->frequency);
}

PandoraEsp32Cc1101* pandora_esp32_cc1101_alloc(PandoraEsp32RxEdgeCb rx_cb, void* ctx) {
    PandoraEsp32Cc1101* cc = malloc(sizeof(PandoraEsp32Cc1101));
    memset(cc, 0, sizeof(PandoraEsp32Cc1101));
    cc->rx_cb = rx_cb;
    cc->rx_ctx = ctx;
    cc->frequency = 433920000; /* default 433.92 MHz band (PandoraARM radio_init) */
    cc->tuned_frequency = cc->frequency;
    cc->cc_preset = CcPresetOok;
    cc->rf_mode = PROTO_RF_IDLE;

    furi_hal_subghz_reset();
    furi_hal_subghz_load_custom_preset(PRESET_OOK650);
    furi_hal_subghz_set_frequency_and_path(cc->frequency);
    furi_hal_subghz_idle();
    cc->radio_on = true;
    FURI_LOG_I(TAG, "CC1101 init OOK650 @%lu Hz", (unsigned long)cc->frequency);
    return cc;
}

void pandora_esp32_cc1101_free(PandoraEsp32Cc1101* cc) {
    furi_assert(cc);
    if(cc->radio_on) {
        if(cc->tx_active) {
            furi_hal_subghz_stop_async_tx();
            cc->tx_active = false;
        }
        if(cc->rx_running) {
            furi_hal_subghz_stop_async_rx();
            cc->rx_running = false;
        }
        furi_hal_subghz_idle();
        furi_hal_subghz_sleep();
        cc->radio_on = false;
    }
    free(cc);
}

void pandora_esp32_cc1101_set_mode(
    PandoraEsp32Cc1101* cc,
    uint8_t mode,
    uint8_t modulation,
    uint32_t freq_hz) {
    if(!cc->radio_on) return;

    /* Update the carrier if the ESP32 reported a valid in-band frequency. */
    if(freq_hz != 0 && cc1101_band_ok(freq_hz)) {
        cc->frequency = freq_hz;
    } else if(freq_hz != 0) {
        FURI_LOG_W(
            TAG, "RF freq %lu Hz out of CC1101 band; keeping %lu Hz", (unsigned long)freq_hz,
            (unsigned long)cc->frequency);
    }

    switch(mode) {
    case PROTO_RF_RX:
        cc->rf_mode = PROTO_RF_RX;
        cc_apply_preset(cc, modulation); /* match modulation + freq, idle radio */
        if(!cc->rx_running) {
            furi_hal_subghz_start_async_rx(pandora_esp32_rx_callback, cc);
            cc->rx_running = true;
            FURI_LOG_I(TAG, "RX start @%lu Hz", (unsigned long)cc->frequency);
        }
        break;

    case PROTO_RF_TX:
        /* Prepare for TX: stop RX, match modulation + freq. The actual replay
         * happens when RF_TX edges arrive. */
        cc->rf_mode = PROTO_RF_TX;
        cc_apply_preset(cc, modulation);
        break;

    case PROTO_RF_READY:
    case PROTO_RF_IDLE:
    default:
        cc->rf_mode = mode;
        if(cc->rx_running) {
            furi_hal_subghz_stop_async_rx();
            cc->rx_running = false;
        }
        if(!cc->tx_active) {
            furi_hal_subghz_idle();
        }
        break;
    }
}

uint32_t pandora_esp32_cc1101_tx_edges(
    PandoraEsp32Cc1101* cc,
    const uint8_t* payload,
    size_t len) {
    if(!cc->radio_on) return 0;
    /* Payload: uint16 count (LE) + count * proto_rf_edge_t. */
    if(len < sizeof(uint16_t)) return 0;
    uint16_t count = (uint16_t)(payload[0] | ((uint16_t)payload[1] << 8));
    const uint8_t* p = payload + sizeof(uint16_t);
    size_t avail = len - sizeof(uint16_t);
    if(avail < (size_t)count * sizeof(proto_rf_edge_t)) {
        /* truncated payload: clamp to what we actually have */
        count = (uint16_t)(avail / sizeof(proto_rf_edge_t));
    }

    uint32_t accepted = 0;
    for(uint16_t i = 0; i < count; i++) {
        proto_rf_edge_t e;
        memcpy(&e, p + (size_t)i * sizeof(proto_rf_edge_t), sizeof(e));
        uint32_t head = cc->tx_head;
        uint32_t next = (head + 1) % TX_RING_LEN;
        if(next == cc->tx_tail) break; /* ring full */
        uint32_t dur = e.duration_us;
        if(dur == 0) dur = 1;
        cc->tx_ring[head].level = (e.level != 0);
        cc->tx_ring[head].duration = dur;
        cc->tx_head = next;
        accepted++;
    }

    /* Kick off the async TX if idle and there is something to send. */
    if(!cc->tx_active && cc->tx_head != cc->tx_tail) {
        if(!furi_hal_subghz_is_tx_allowed(cc->frequency)) {
            FURI_LOG_E(TAG, "TX NOT allowed @%lu Hz; dropping", (unsigned long)cc->frequency);
            cc->tx_tail = cc->tx_head;
            return accepted;
        }
        if(cc->rx_running) {
            furi_hal_subghz_stop_async_rx();
            cc->rx_running = false;
            furi_hal_subghz_idle();
        }
        if(furi_hal_subghz_start_async_tx(pandora_esp32_tx_callback, cc)) {
            cc->tx_active = true;
            FURI_LOG_I(TAG, "TX async start: %lu edges", (unsigned long)accepted);
        } else {
            FURI_LOG_E(TAG, "start_async_tx failed");
            cc->tx_tail = cc->tx_head;
        }
    }
    return accepted;
}

void pandora_esp32_cc1101_pump(PandoraEsp32Cc1101* cc) {
    if(!cc->radio_on || !cc->tx_active) return;
    if(furi_hal_subghz_is_async_tx_complete()) {
        furi_hal_subghz_stop_async_tx();
        furi_hal_subghz_idle();
        cc->tx_active = false;
        FURI_LOG_I(TAG, "TX frame sent");
        /* If the ESP32 left us in RX mode, resume listening. */
        if(cc->rf_mode == PROTO_RF_RX && !cc->rx_running) {
            furi_hal_subghz_start_async_rx(pandora_esp32_rx_callback, cc);
            cc->rx_running = true;
        }
    }
}
