#pragma once

/*
 * pandora_esp32_uart.h - UART transport + protocol framing for the ESP32
 * companion. The transport follows the wifi_marauder_companion pattern:
 *   - furi_hal_serial on FuriHalSerialIdUsart @ 230400,
 *   - an async RX IRQ callback that pushes raw bytes into a FuriStreamBuffer,
 *   - a worker FuriThread that drains the stream and runs the frame parser,
 *   - furi_hal_serial_tx for sending.
 *
 * On top of that transport this module implements the PROTO_* framing
 * (SYNC | TYPE | LEN | PAYLOAD | CRC8) from pandora_esp32_protocol.h. Decoded
 * frames are delivered to a user callback; outgoing frames are encoded and
 * TX'd here so callers only ever deal with (type, payload, len).
 */

#include <furi.h>
#include <furi_hal.h>

typedef struct PandoraEsp32Uart PandoraEsp32Uart;

/* Called from the UART worker thread for every fully-decoded, CRC-valid frame.
 * `payload` is owned by the UART layer and only valid for the duration of the
 * call; copy anything you need to keep. */
typedef void (*PandoraEsp32FrameCb)(uint8_t type, const uint8_t* payload, size_t len, void* ctx);

/* Allocate + start the UART transport and parser worker. */
PandoraEsp32Uart* pandora_esp32_uart_init(PandoraEsp32FrameCb cb, void* ctx);

/* Stop + free the UART transport and parser worker. */
void pandora_esp32_uart_free(PandoraEsp32Uart* uart);

/* Encode and transmit one protocol frame. `payload` may be NULL if len == 0.
 * Blocks on the serial TX like the marauder companion. */
void pandora_esp32_uart_send(
    PandoraEsp32Uart* uart,
    uint8_t type,
    const uint8_t* payload,
    size_t len);
