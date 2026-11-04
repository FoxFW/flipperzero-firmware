#pragma once

/*
 * pandora_esp32_cc1101.h - CC1101 radio bridge for the ESP32 companion.
 *
 * Ported from PandoraARM's "CC1101 bridge" (main.c). Differences vs PandoraARM:
 *   - PandoraARM runs the keyfob firmware locally and DERIVES the RF state /
 *     captures TX edges from the emulated Si4432. Here the ESP32 does all that
 *     and tells us over UART:
 *       * RF_MODE  -> we configure the CC1101 (freq + OOK/FSK preset + RX/TX).
 *       * RF_TX    -> a ready-made list of (level,duration_us) edges to replay
 *                     with furi_hal_subghz_start_async_tx.
 *   - When the CC1101 is in RX, the edges it delivers from the air are handed
 *     back to the caller (which forwards them to the ESP32 as RX_EDGE).
 *
 * The OOK/2FSK presets and the Si4432->CC1101 preset selection are copied
 * verbatim from PandoraARM.
 */

#include <furi.h>
#include <furi_hal.h>
#include <stdint.h>

typedef struct PandoraEsp32Cc1101 PandoraEsp32Cc1101;

/* Called (from the subghz RX IRQ) for every edge received off the air. Keep it
 * short + non-blocking; forward the edge to the ESP32 elsewhere. */
typedef void (*PandoraEsp32RxEdgeCb)(bool level, uint32_t duration_us, void* ctx);

PandoraEsp32Cc1101* pandora_esp32_cc1101_alloc(PandoraEsp32RxEdgeCb rx_cb, void* ctx);
void pandora_esp32_cc1101_free(PandoraEsp32Cc1101* cc);

/* Apply a radio mode coming from the ESP32 (PROTO_MSG_RF_MODE):
 *   mode       : PROTO_RF_IDLE / READY / RX / TX
 *   modulation : PROTO_MOD_* (OOK vs FSK preset)
 *   freq_hz    : carrier frequency (0 => keep current / default 433.92 MHz). */
void pandora_esp32_cc1101_set_mode(
    PandoraEsp32Cc1101* cc,
    uint8_t mode,
    uint8_t modulation,
    uint32_t freq_hz);

/* Queue + transmit a waveform coming from the ESP32 (PROTO_MSG_RF_TX). The
 * edges array is copied into the internal ring. Returns the number of edges
 * accepted. */
uint32_t pandora_esp32_cc1101_tx_edges(
    PandoraEsp32Cc1101* cc,
    const uint8_t* payload,
    size_t len);

/* Must be called periodically from the main loop: completes any in-flight
 * async TX and tears it down when done. */
void pandora_esp32_cc1101_pump(PandoraEsp32Cc1101* cc);
