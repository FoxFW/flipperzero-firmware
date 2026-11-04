#pragma once

// NFC HF + LF channel. args: "READ_HF|EMU_HF|WRITE_HF <file.nfc>" or
// "READ_LF|EMU_LF|WRITE_LF <file.rfid> [ms]". Returns a short status string ("ok", "no-tag", ...).
const char* fox_nfc_channel(const char* args);
