#pragma once

#include <furi.h>
#include <m-array.h>

#define SUBGHZ_KEYSTORE_MAX_FILES 4

ARRAY_DEF(SubGhzKeystoreNameArray, FuriString*, M_POD_OPLIST)

struct SubGhzKeystore {
    SubGhzKeyArray_t data;
    const char* mfname;
    uint8_t kl_type;
    FuriString* files[SUBGHZ_KEYSTORE_MAX_FILES];
    size_t file_count;
    SubGhzKeystoreNameArray_t names;
    FuriMutex* names_mutex;
};
