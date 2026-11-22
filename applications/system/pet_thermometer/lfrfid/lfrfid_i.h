#pragma once
// Minimal lfrfid_i.h for the fox_nfc_io embedded LF engine — just the file/path
// defines the engine files reference. The stock app's lfrfid_i.h pulls in GUI /
// scenes, which a headless FAP must not; the engine itself only needs these.
#include <furi.h>
#include <storage/storage.h>

#define LFRFID_KEY_NAME_SIZE   22
#define LFRFID_TEXT_STORE_SIZE 40

#define LFRFID_APP_FOLDER                    EXT_PATH("lfrfid")
#define LFRFID_SD_FOLDER                     EXT_PATH("lfrfid")
#define LFRFID_APP_FILENAME_PREFIX           "RFID"
#define LFRFID_APP_FILENAME_EXTENSION        ".rfid"
#define LFRFID_APP_SHADOW_FILENAME_EXTENSION ".shd"
#define LFRFID_APP_RAW_ASK_EXTENSION         ".ask.raw"
#define LFRFID_APP_RAW_PSK_EXTENSION         ".psk.raw"

// Minimal app-context shim: the engine's write-and-set-password mode casts
// worker->cb_ctx to LfRfid* for a 4-byte T5577 password. fox_nfc_io uses plain
// write (no password), so that path never runs; this only satisfies the type.
typedef struct LfRfid { uint8_t password[4]; } LfRfid;
