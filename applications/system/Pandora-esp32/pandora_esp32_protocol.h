/*
 * pandora_esp32_protocol.h - Shared UART framing between the ESP32 (emulator
 *              backend) and the Flipper Zero (display + keypad + radio frontend).
 *
 * This header is intended to be COMPILED BY BOTH SIDES (ESP32 firmware and the
 * Flipper companion app). It only declares constants and the frame layout; no
 * platform-specific types are used (only <stdint.h>).
 *
 * KEEP THIS FILE BYTE-FOR-BYTE IDENTICAL to the ESP32 copy at
 *   ARM-Cortex-M3-Emulator-for-Flipper/main/protocol.h
 * (only the leading comment's file name differs between the two copies; the
 * declarations below MUST match exactly).
 *
 * WIRE FORMAT (every frame, both directions):
 *
 *     +------+------+------+-----------------+------+
 *     | SYNC | TYPE | LEN  |   PAYLOAD[LEN]  | CRC8 |
 *     +------+------+------+-----------------+------+
 *       0xA5   1B     2B (LE)   LEN bytes       1B
 *
 *   - SYNC  : 0xA5 start-of-frame marker (lets the receiver resynchronise).
 *   - TYPE  : one of PROTO_* below.
 *   - LEN   : payload length, little-endian uint16 (0..PROTO_MAX_PAYLOAD).
 *   - CRC8  : CRC-8 (poly 0x07, init 0x00) over TYPE + LEN(2) + PAYLOAD.
 *
 * The CRC does NOT cover the SYNC byte (the SYNC is only a resync aid).
 *
 * AGNOSTIC NOTE (protocol v2): the emulator is now AGNOSTIC of the firmware. The
 * HARDWARE is identical for every firmware of this keyfob, so there is NO profile
 * selection any more. The uploaded .hex can be ANY firmware for this MCU. The
 * unlock KNOCK-CODE (which IS firmware config, not hardware) is sent explicitly by
 * the Flipper via PROTO_MSG_UNLOCK as a button SEQUENCE, instead of being derived
 * from a per-firmware profile id. FW_BEGIN no longer carries a profile field.
 */
#ifndef PANDORA_PROTOCOL_H
#define PANDORA_PROTOCOL_H

#include <stdint.h>

#define PROTO_SYNC 0xA5u

/* Keep this >= the OLED framebuffer (1472B) plus some slack for RF_TX edges. */
#define PROTO_MAX_PAYLOAD 2048u

/* OLED framebuffer geometry (keyfob UC16xx/ST7528): 92 columns x 16 pages. */
#define PROTO_OLED_COLS 92u
#define PROTO_OLED_PAGES 16u
#define PROTO_FB_SIZE (PROTO_OLED_COLS * PROTO_OLED_PAGES) /* 1472 bytes */

/* -------------------------------------------------------- ESP32 -> Flipper -- */
/* Full framebuffer dump (payload = PROTO_FB_SIZE raw bytes, page-addressed,
 * 2bpp, bit0 = top row). The Flipper renders it on its 128x64 screen. */
#define PROTO_MSG_FB 0x01u
/* Radio state. payload: struct proto_rf_mode (see below). */
#define PROTO_MSG_RF_MODE 0x02u
/* RF waveform to transmit. payload: uint16 count + count * proto_rf_edge. */
#define PROTO_MSG_RF_TX 0x03u
/* Debug log line. payload: ASCII text (not NUL-terminated on the wire). */
#define PROTO_MSG_LOG 0x04u
/* Handshake / keepalive reply. payload: ASCII banner. */
#define PROTO_MSG_PONG 0x05u
/* Result after FW_END. payload: proto_fw_ack_t {ok, flat_size}. ok=1 if the HEX
 * parsed and the emulator initialized successfully. */
#define PROTO_MSG_FW_ACK 0x06u
/* Optional progress during upload. payload: proto_fw_progress_t. */
#define PROTO_MSG_FW_PROGRESS 0x07u
#define PROTO_MSG_BEEP 0x08u /* firmware-driven buzzer activity */

/* -------------------------------------------------------- Flipper -> ESP32 -- */
/* Button state. payload: 1 byte bitmask (PROTO_BTN_*). */
#define PROTO_MSG_BTN 0x81u
/* A received RF edge from the air. payload: proto_rf_edge. Injected into PB0. */
#define PROTO_MSG_RX_EDGE 0x82u
/* Handshake / keepalive request. payload: optional ASCII. ESP32 replies PONG. */
#define PROTO_MSG_PING 0x83u
/* Request a full framebuffer resend (e.g. on Flipper (re)connect). */
#define PROTO_MSG_REQ_FB 0x84u
/* Reset + re-run the last unlock knock on the emulator. payload: none. */
#define PROTO_MSG_RESET 0x85u

/* ------------------------------------------- firmware upload (Flipper -> ESP32)
 *
 * Instead of embedding the keyfob firmware in the ESP32 flash, the Flipper reads
 * the firmware .hex (Intel HEX, ASCII) from its SD card and streams it to the
 * ESP32 over this same UART link. The ESP32 parses the Intel HEX INCREMENTALLY
 * (line by line, as chunks arrive) straight into a flat flash image, so the raw
 * .hex text (~193 KB) is NEVER buffered whole. On FW_END the ESP32 (re)inits the
 * emulator with the flat image. The firmware is AGNOSTIC: any .hex for this MCU.
 *
 * Sequence:
 *   Flipper: FW_BEGIN {total_hex_bytes}             -> resets the HEX parser
 *   Flipper: FW_CHUNK <raw .hex ASCII bytes> ...    -> parsed incrementally
 *   Flipper: FW_END   {crc32? (optional)}           -> finalize + emu_init
 *   ESP32  : FW_ACK   {ok, flat_size}               -> result of the whole load
 *   Flipper: UNLOCK   {knock sequence}              -> emu_set_knock + unlock
 *
 * FW_PROGRESS (ESP32 -> Flipper) is optional and only informational.
 */

/* Begin a firmware upload. payload: proto_fw_begin_t. Resets the HEX receiver. */
#define PROTO_MSG_FW_BEGIN 0x86u
/* One chunk of raw Intel HEX text (ASCII). payload: up to PROTO_MAX_PAYLOAD raw
 * bytes of the .hex file. Chunks are concatenated by the receiver and parsed
 * line by line (CRLF/LF tolerant); partial trailing lines are carried over to
 * the next chunk. */
#define PROTO_MSG_FW_CHUNK 0x87u
/* End of firmware upload. payload: none, OR proto_fw_end_t (optional CRC32 of
 * the full .hex text for integrity). Triggers HEX finalize + emu_init. */
#define PROTO_MSG_FW_END 0x88u
/* Batched CC1101 RX edges: uint16 count (LE) followed by count proto_rf_edge_t.
 * Preferred over PROTO_MSG_RX_EDGE for throughput; the ESP32 injects each edge
 * into the firmware demodulator (PB0 + TIMER1 capture) in order. */
#define PROTO_MSG_RX_EDGES 0x89u
/* Stop/reset the remote emulator when the Flipper frontend exits. payload: none. */
#define PROTO_MSG_STOP 0x8Au
/* AUTO-UNLOCK by knock-code (Flipper -> ESP32). payload: proto_unlock_t header
 * followed by `count` proto_knock_step_t. The ESP32 installs the sequence with
 * emu_set_knock() and then runs emu_reset_and_unlock() (presses it over GPIO).
 * Send this AFTER FW_ACK. A count of 0 disables auto-unlock (manual nav only). */
#define PROTO_MSG_UNLOCK 0x8Bu

/* ------------------------------------------------------------ button bits --- */
/* One bit per keyfob logical button. Matches the emulator's B_* enum order
 * (B_UP=0, B_DOWN=1, B_OK=2, B_BACK=3, B5=4, B6=5). These bit positions are also
 * reused as the knock-step button ids in proto_knock_step_t.button below. */
#define PROTO_BTN_UP (1u << 0)
#define PROTO_BTN_DOWN (1u << 1)
#define PROTO_BTN_OK (1u << 2)
#define PROTO_BTN_BACK (1u << 3)
#define PROTO_BTN_B5 (1u << 4)
#define PROTO_BTN_B6 (1u << 5)

/* Logical keyfob button INDICES (NOT bitmasks). Used by the knock-code steps and
 * the unlock gate; identical to the emulator's B_* enum so the ESP32 can pass
 * them straight to emu_set_knock() / emu_press_button(). */
#define PROTO_BTN_ID_UP 0u
#define PROTO_BTN_ID_DOWN 1u
#define PROTO_BTN_ID_OK 2u
#define PROTO_BTN_ID_BACK 3u
#define PROTO_BTN_ID_B5 4u
#define PROTO_BTN_ID_B6 5u
#define PROTO_BTN_ID_NONE 0xFFu /* "no gate" sentinel for proto_unlock_t.gate */

/* Maximum number of knock steps carried in one PROTO_MSG_UNLOCK. Matches the
 * emulator's knock_seq[16] capacity. */
#define PROTO_KNOCK_MAX_STEPS 16u

/* ------------------------------------------------------------ RF state ------ */
/* Mirrors the Si4432 operating mode derived in the emulator. */
#define PROTO_RF_IDLE 0u
#define PROTO_RF_READY 1u
#define PROTO_RF_RX 2u
#define PROTO_RF_TX 3u

/* Radio modulation (for display/diagnostics on the Flipper). */
#define PROTO_MOD_UNMOD 0u
#define PROTO_MOD_OOK 1u
#define PROTO_MOD_FSK 2u
#define PROTO_MOD_GFSK 3u

/* Packed to keep the wire layout identical on both compilers. */
#if defined(__GNUC__)
#define PROTO_PACKED __attribute__((packed))
#else
#define PROTO_PACKED
#endif

typedef struct PROTO_PACKED {
    uint8_t mode; /* PROTO_RF_* */
    uint8_t modulation; /* PROTO_MOD_* */
    uint32_t freq_hz; /* carrier frequency in Hz (0 if unknown) */
} proto_rf_mode_t;

typedef struct PROTO_PACKED {
    uint8_t level; /* 0 = low, 1 = high */
    uint32_t duration_us; /* edge duration in microseconds */
} proto_rf_edge_t;

/* ----------------------------------------------------- firmware upload ------- */
/* FW_BEGIN payload. AGNOSTIC: no profile field -- any .hex for this MCU loads
 * onto the single hardware model. */
typedef struct PROTO_PACKED {
    uint32_t total_hex_bytes; /* size of the .hex text to be streamed (hint) */
} proto_fw_begin_t;

/* FW_END payload (optional; LEN may be 0 to skip the CRC check). */
typedef struct PROTO_PACKED {
    uint32_t crc32; /* CRC-32 of the full .hex text, or 0 if not provided */
} proto_fw_end_t;

/* FW_ACK payload (ESP32 -> Flipper, sent after FW_END). */
typedef struct PROTO_PACKED {
    uint8_t ok; /* 1 = parse + emu_init OK, 0 = failure */
    uint32_t flat_size; /* size of the parsed flat flash image in bytes */
} proto_fw_ack_t;

/* FW_PROGRESS payload (ESP32 -> Flipper, optional/informational). */
typedef struct PROTO_PACKED {
    uint32_t hex_bytes_seen; /* raw .hex bytes consumed so far */
    uint32_t flat_bytes; /* highest flat address written so far + 1 */
} proto_fw_progress_t;

/* ----------------------------------------------------- auto-unlock knock ----
 *
 * The knock-code is a SEQUENCE OF BUTTON PRESSES (the real Pandora keyfobs are
 * unlocked with a button combo, e.g. UP,OK,OK,UP,UP,DOWN followed by holding a
 * gate button). It is FIRMWARE CONFIG, so the Flipper owns it and sends it here;
 * the agnostic emulator just presses it over GPIO at reset.
 *
 * PROTO_MSG_UNLOCK payload layout:
 *     proto_unlock_t                       (header)
 *     proto_knock_step_t  step[count]      (the sequence, count from the header)
 *
 * Each step is one button tap. `hold` marks a long-press tap. The final held
 * GATE button (held down while the firmware validates the combo) is given once
 * in the header `gate` field (a PROTO_BTN_ID_*, or PROTO_BTN_ID_NONE).
 */
typedef struct PROTO_PACKED {
    uint8_t button; /* PROTO_BTN_ID_* (logical keyfob button index) */
    uint8_t hold; /* 0 = short tap, 1 = long press */
} proto_knock_step_t;

typedef struct PROTO_PACKED {
    uint8_t count; /* number of proto_knock_step_t that follow (0..16) */
    uint8_t gate; /* PROTO_BTN_ID_* final held gate button, or PROTO_BTN_ID_NONE */
} proto_unlock_t;

/* ------------------------------------------------------------ CRC-8 --------- */
/* CRC-8, polynomial 0x07, init 0x00 (a.k.a. CRC-8/SMBUS without final XOR).
 * Small, branch-light, header-only so both sides share the exact same impl. */
static inline uint8_t proto_crc8(const uint8_t* data, uint32_t len) {
    uint8_t crc = 0x00u;
    for(uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for(int b = 0; b < 8; b++) {
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x07u) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

#endif /* PANDORA_PROTOCOL_H */
