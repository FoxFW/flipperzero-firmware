# [ESP32] Pandora

Flipper Zero **companion** app for an **ESP32-WROOM-32** running the Pandora
D-605 keyfob emulator.

## What it does

The heavy emulation (the REAL keyfob firmware) runs on the ESP32. The Flipper is
just the **display + keypad + radio** front-end. It is **firmware-agnostic**: any
`.hex` for this keyfob MCU works, with **no profile selection**.

### Launch menu (new flow)

On start, after the ESP32 UART link is up, you get a small **menu**:

1. **Firmware** — press OK/Right to open the SD file browser and pick a `.hex`.
2. **Knock** — press OK/Right to open the **knock-sequence editor** (the keyfob
   unlock button-combo). Left/Right move the cursor, Up/Down change the button of
   the current step, **short OK** toggles long-press (`*`), **long OK** inserts a
   step, **long BACK** deletes a step, **short BACK** returns to the menu.
3. **Gate** — Left/Right cycle the final held gate button (`none`, `UP`..`B6`).
   The gate is held down while the firmware validates the combo (default `B5`).
4. **Launch** — press OK: the app uploads the firmware (`FW_BEGIN`/`FW_CHUNK*`/
   `FW_END`), then sends `PROTO_MSG_UNLOCK` with your knock so the ESP32
   **auto-unlocks over GPIO**.

The default knock is the classic Pandora combo `UP,OK,OK,UP,UP,DOWN` + hold `B5`.

**If the knock is wrong**, the firmware just stays on its locked screen — unlock
**manually** with the Flipper buttons (see the button map below), or go back and
fix the knock and Launch again.

Then:

1. **Firmware upload** — the chosen `.hex` is streamed to the ESP32 over UART
   (`FW_BEGIN` / `FW_CHUNK*` / `FW_END`). The ESP32 flattens + boots it and
   replies `FW_ACK`.
2. **Display** — the ESP32 streams the keyfob OLED framebuffer (`PROTO_MSG_FB`,
   92×16 @ 2bpp). The Flipper renders the 88 visible columns, centered on the
   128×64 screen.
3. **Keypad** — the Flipper D-pad is forwarded to the ESP32 as a button bitmask
   (`PROTO_MSG_BTN`).
4. **Radio** — the ESP32 tells the Flipper the RF state (`PROTO_MSG_RF_MODE`) and
   sends ready-made TX waveforms (`PROTO_MSG_RF_TX`) which the Flipper transmits
   on the internal **CC1101**. Edges received off the air are sent back to the
   ESP32 (`PROTO_MSG_RX_EDGE`). Default band: **433.92 MHz**.

## Wiring (UART)

The app uses `FuriHalSerialIdUsart` at **230400 8N1**. The ESP32 companion
firmware must be rebuilt and flashed with the matching baud rate.

| ESP32              | Flipper            |
|--------------------|--------------------|
| GPIO1 / TX0  →     | RX (pin 14, `RX`)  |
| GPIO3 / RX0  ←     | TX (pin 13, `TX`)  |
| GND                | GND (pin 8/11/18)  |
| 3V3 (or 5V/VBUS)   | 3V3 (pin 9) / 5V   |

> Cross TX↔RX. Share a common ground. Power the ESP32 from 3V3/5V as your board
> requires.

## Firmware images on the SD

Put your keyfob `.hex` files in:

```
/ext/apps_data/pandora/
```

The menu's **Firmware** item opens a file browser there (falls back to `/ext` if
the folder is missing) filtered to `*.hex`. There is **no profile**: the emulator
is agnostic, so any `.hex` for this MCU loads onto the single hardware model.

## Button map (Flipper D-pad → keyfob logical button)

Final **1:1 agnostic mapping** (all six keyfob buttons reachable, intuitive):

| Flipper key | Keyfob button |
|-------------|---------------|
| UP          | UP   (1)      |
| OK          | OK   (2)      |
| DOWN        | DOWN (3)      |
| LEFT        | BACK (4)      |
| RIGHT       | B5   (5)      |
| BACK        | B6   (6)      |

- **Hold BACK > 1 s** = exit the app (host-side). A **short** BACK is still a
  keyfob **B6** tap; the >1 s exit window sits *above* the keyfob's ~500 ms–1 s
  long-press, so exiting never steals a button function. `UP` stays free for
  normal keyfob use. Exit works in the menu, the editor, the firmware screen and
  after an ESP32 link timeout.

The ESP32 backend must be rebuilt after updating `esp32-pandora/main/main.c`.
It converts each newly pressed button bit into the emulator's scanned key
cycle (`emu_nav_button`) and releases the virtual GPIO afterward. This keeps
short Flipper taps from falling between MCU execution bursts.

## Protocol

The UART wire format is defined in `pandora_esp32_protocol.h`, a verbatim copy of
`ARM-Cortex-M3-Emulator-for-Flipper/main/protocol.h` (byte-for-byte identical from
`#ifndef` onward; only the leading comment's file name differs) so both sides
share the exact same framing:

```
SYNC(0xA5) | TYPE | LEN(2 LE) | PAYLOAD | CRC8(poly 0x07 over TYPE+LEN+PAYLOAD)
```

### Auto-unlock knock (`PROTO_MSG_UNLOCK` = 0x8B)

After `FW_ACK`, the Flipper sends the knock as a button **sequence**:

```
proto_unlock_t { uint8 count; uint8 gate; }      // gate = PROTO_BTN_ID_* or 0xFF (none)
proto_knock_step_t { uint8 button; uint8 hold; } * count   // button = PROTO_BTN_ID_*
```

The ESP32 installs it with `emu_set_knock()` and presses it over GPIO
(`emu_reset_and_unlock`). `count = 0` disables auto-unlock (manual only).

## Build

```
cd Flipper-ARF
./fbt fap_pandora_esp32
```
