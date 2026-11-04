#include "pandora_esp32_uart.h"
#include "pandora_esp32_protocol.h"

#include <string.h>

#define UART_CH (FuriHalSerialIdUsart)
/* Fox_ESP32_FW shares UART0 and boots its AT console at SERIAL_BAUD (115200),
 * so the Pandora bridge runs at the same baud (single-baud handshake). A future
 * throughput bump would parse Fox's "PANDORA ENTER <baud>" reply and re-init. */
#define BAUDRATE (115200)

/* Raw byte ring between the RX IRQ and the worker thread. Must hold at least a
 * full framebuffer frame (1472B payload + 5B overhead) with slack. */
#define RX_STREAM_SIZE (4096)
/* Scratch the worker uses to pull bytes out of the stream in one go. */
#define RX_DRAIN_SIZE (1024)

/* Frame parser states. */
typedef enum {
    ParseSync = 0, /* waiting for SYNC (0xA5) */
    ParseType, /* got SYNC, reading TYPE */
    ParseLenLo, /* reading LEN low byte */
    ParseLenHi, /* reading LEN high byte */
    ParsePayload, /* reading PAYLOAD[LEN] */
    ParseCrc, /* reading CRC8 */
} ParseState;

struct PandoraEsp32Uart {
    FuriThread* rx_thread;
    FuriStreamBuffer* rx_stream;
    FuriHalSerialHandle* serial_handle;

    PandoraEsp32FrameCb frame_cb;
    void* frame_ctx;

    /* --- incremental frame parser state (worker-thread only) --- */
    ParseState state;
    uint8_t cur_type;
    uint16_t cur_len;
    uint16_t cur_got; /* payload bytes received so far */
    uint8_t payload[PROTO_MAX_PAYLOAD];
};

typedef enum {
    WorkerEvtStop = (1 << 0),
    WorkerEvtRxDone = (1 << 1),
} WorkerEvtFlags;

#define WORKER_ALL_RX_EVENTS (WorkerEvtStop | WorkerEvtRxDone)

/* Feed one raw byte into the incremental frame parser. On a complete,
 * CRC-valid frame, dispatch it to the user callback. */
static void pandora_esp32_parse_byte(PandoraEsp32Uart* uart, uint8_t b) {
    switch(uart->state) {
    case ParseSync:
        if(b == PROTO_SYNC) uart->state = ParseType;
        break;

    case ParseType:
        uart->cur_type = b;
        uart->state = ParseLenLo;
        break;

    case ParseLenLo:
        uart->cur_len = b;
        uart->state = ParseLenHi;
        break;

    case ParseLenHi:
        uart->cur_len |= (uint16_t)b << 8;
        if(uart->cur_len > PROTO_MAX_PAYLOAD) {
            /* Bogus length: resync from scratch. */
            uart->state = ParseSync;
            break;
        }
        uart->cur_got = 0;
        uart->state = (uart->cur_len == 0) ? ParseCrc : ParsePayload;
        break;

    case ParsePayload:
        uart->payload[uart->cur_got++] = b;
        if(uart->cur_got >= uart->cur_len) uart->state = ParseCrc;
        break;

    case ParseCrc: {
        /* CRC-8 over TYPE + LEN(2 LE) + PAYLOAD. */
        uint8_t hdr[3] = {
            uart->cur_type,
            (uint8_t)(uart->cur_len & 0xFF),
            (uint8_t)(uart->cur_len >> 8),
        };
        uint8_t crc = proto_crc8(hdr, sizeof(hdr));
        /* continue CRC over payload */
        for(uint16_t i = 0; i < uart->cur_len; i++) {
            crc ^= uart->payload[i];
            for(int k = 0; k < 8; k++) {
                crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x07u) : (uint8_t)(crc << 1);
            }
        }
        if(crc == b && uart->frame_cb) {
            uart->frame_cb(uart->cur_type, uart->payload, uart->cur_len, uart->frame_ctx);
        }
        uart->state = ParseSync;
        break;
    }

    default:
        uart->state = ParseSync;
        break;
    }
}

/* RX IRQ: push every received byte into the stream buffer (marauder pattern).
 * Keep this minimal and non-blocking. */
static void pandora_esp32_uart_on_irq_cb(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
    PandoraEsp32Uart* uart = (PandoraEsp32Uart*)context;
    if(event == FuriHalSerialRxEventData) {
        uint8_t data = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(uart->rx_stream, &data, 1, 0);
        furi_thread_flags_set(furi_thread_get_id(uart->rx_thread), WorkerEvtRxDone);
    }
}

static int32_t pandora_esp32_uart_worker(void* context) {
    PandoraEsp32Uart* uart = (PandoraEsp32Uart*)context;
    uint8_t drain[RX_DRAIN_SIZE];

    while(1) {
        uint32_t events =
            furi_thread_flags_wait(WORKER_ALL_RX_EVENTS, FuriFlagWaitAny, FuriWaitForever);
        furi_check((events & FuriFlagError) == 0);
        if(events & WorkerEvtStop) break;
        if(events & WorkerEvtRxDone) {
            size_t len;
            /* Drain everything currently queued, byte-feeding the parser. */
            do {
                len = furi_stream_buffer_receive(uart->rx_stream, drain, sizeof(drain), 0);
                for(size_t i = 0; i < len; i++) {
                    pandora_esp32_parse_byte(uart, drain[i]);
                }
            } while(len == sizeof(drain));
        }
    }

    return 0;
}

void pandora_esp32_uart_send(
    PandoraEsp32Uart* uart,
    uint8_t type,
    const uint8_t* payload,
    size_t len) {
    furi_assert(uart);
    if(len > PROTO_MAX_PAYLOAD) return;

    /* Build the frame: SYNC | TYPE | LEN(2 LE) | PAYLOAD | CRC8. */
    uint8_t hdr[4];
    hdr[0] = PROTO_SYNC;
    hdr[1] = type;
    hdr[2] = (uint8_t)(len & 0xFF);
    hdr[3] = (uint8_t)((len >> 8) & 0xFF);

    /* CRC over TYPE + LEN(2) + PAYLOAD (NOT the SYNC). */
    uint8_t crc;
    {
        uint8_t chdr[3] = {hdr[1], hdr[2], hdr[3]};
        crc = proto_crc8(chdr, sizeof(chdr));
        for(size_t i = 0; i < len; i++) {
            crc ^= payload[i];
            for(int k = 0; k < 8; k++) {
                crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x07u) : (uint8_t)(crc << 1);
            }
        }
    }

    furi_hal_serial_tx(uart->serial_handle, hdr, sizeof(hdr));
    if(len) furi_hal_serial_tx(uart->serial_handle, (uint8_t*)payload, len);
    furi_hal_serial_tx(uart->serial_handle, &crc, 1);
}

PandoraEsp32Uart* pandora_esp32_uart_init(PandoraEsp32FrameCb cb, void* ctx) {
    PandoraEsp32Uart* uart = malloc(sizeof(PandoraEsp32Uart));
    if(!uart) return NULL;
    memset(uart, 0, sizeof(PandoraEsp32Uart));

    uart->frame_cb = cb;
    uart->frame_ctx = ctx;
    uart->state = ParseSync;

    uart->rx_stream = furi_stream_buffer_alloc(RX_STREAM_SIZE, 1);
    uart->rx_thread = furi_thread_alloc();
    if(!uart->rx_stream || !uart->rx_thread) {
        if(uart->rx_thread) furi_thread_free(uart->rx_thread);
        if(uart->rx_stream) furi_stream_buffer_free(uart->rx_stream);
        free(uart);
        return NULL;
    }
    furi_thread_set_name(uart->rx_thread, "PandoraEsp32UartRx");
    /* The worker owns a 1 KiB drain buffer and invokes the frame callback;
     * leave stack for CRC parsing and callback frames as well. */
    furi_thread_set_stack_size(uart->rx_thread, 2048);
    furi_thread_set_context(uart->rx_thread, uart);
    furi_thread_set_callback(uart->rx_thread, pandora_esp32_uart_worker);
    furi_thread_start(uart->rx_thread);

    uart->serial_handle = furi_hal_serial_control_acquire(UART_CH);
    furi_check(uart->serial_handle);
    furi_hal_serial_init(uart->serial_handle, BAUDRATE);
    furi_hal_serial_async_rx_start(
        uart->serial_handle, pandora_esp32_uart_on_irq_cb, uart, false);

    /* Fox_ESP32_FW integration: Fox boots in its AT console on this shared UART.
     * Send the "pandora" AT command (raw ASCII) so Fox hands UART0 over to the
     * binary Pandora protocol. Fox's "PANDORA ENTER <baud>" ASCII reply is skipped
     * harmlessly by the frame parser (ParseSync drops any non-0xA5 byte). */
    static const uint8_t enter_cmd[] = "pandora\n";
    furi_hal_serial_tx(uart->serial_handle, enter_cmd, sizeof(enter_cmd) - 1);
    furi_delay_ms(50);

    return uart;
}

void pandora_esp32_uart_free(PandoraEsp32Uart* uart) {
    furi_assert(uart);

    furi_hal_serial_async_rx_stop(uart->serial_handle);
    furi_hal_serial_deinit(uart->serial_handle);
    furi_hal_serial_control_release(uart->serial_handle);

    furi_thread_flags_set(furi_thread_get_id(uart->rx_thread), WorkerEvtStop);
    furi_thread_join(uart->rx_thread);
    furi_thread_free(uart->rx_thread);

    furi_stream_buffer_free(uart->rx_stream);
    free(uart);
}
