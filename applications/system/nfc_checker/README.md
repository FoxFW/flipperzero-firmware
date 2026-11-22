# NFC Checker

NFC Checker is a vibe-coded external Flipper Zero app that compares a reference NFC tag
with an emulated copy of itself to test if a real world reader would accept it as original. It reports an **Emulation Success Score** and
shows which properties match, differ, or could not be checked.

## Features

- Read a tag directly with the Flipper Zero, or select a saved `.nfc` file.
- Compare a reference tag (the **baseline**) with a second tag (the **emulated**).
- Check UID, protocol, and ATQA/SAK where applicable.
- Compare MIFARE Classic blocks and known keys, and MIFARE Ultralight/NTAG pages,
  when the selected files contain those data.
- Review a weighted score, verdict, and per-check details on the Flipper screen.
- Optionally save a live-read baseline as an `.nfc` file.

## Score and verdict

The score is calculated from applicable checks, with these weights:

| Check | Weight |
|---|---:|
| UID | 40 |
| Protocol | 15 |
| ATQA | 10 |
| SAK | 10 |
| Memory | 20 |
| MIFARE Classic keys | 5 |

The verdict thresholds are:

| Score | Verdict |
|---|---|
| 95–100, with no failed checks | Accepted |
| 80–94 | Likely |
| 50–79 | Risky |
| 0–49 | Rejected |

**Note that reader behavior can depend on factors that this app does not measure**

## Build and install

NFC Checker is an external app for the Flipper Zero (`f7` target) and requires
the firmware NFC module.

1. Set up the [Flipper Zero firmware build environment](https://github.com/flipperdevices/flipperzero-firmware)
   or [uFBT](https://github.com/flipperdevices/flipperzero-ufbt).
2. Build the app from this directory using your chosen toolchain. With uFBT,
   run:

   ```sh
   ufbt
   ```

3. Install the generated `.fap` on your Flipper Zero using qFlipper or another
   compatible FAP installer.
4. Find the app in the Flipper's **NFC** app category.

Build and installation steps can vary with firmware and toolchain versions;
consult their documentation if your environment uses a different workflow.

## Project files

- `application.fam` — Flipper app metadata and build configuration.
- `nfc_checker_app.c` / `nfc_checker_app.h` — menu, workflow, and results UI.
- `nfc_checker_nfc.c` / `nfc_checker_nfc.h` — live reads and `.nfc` file I/O.
- `nfc_checker_tag.c` / `nfc_checker_tag.h` — normalized tag snapshots.
- `nfc_checker_compare.c` / `nfc_checker_compare.h` — comparison checks and score calculation.
  
## Credits

- [XEmaz](https://github.com/xemaz) - main developer
- [Claude](https://claude.ai) - code assistance and debugging
