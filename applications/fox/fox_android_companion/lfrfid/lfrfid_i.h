#pragma once

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

typedef struct LfRfid { uint8_t password[4]; } LfRfid;
