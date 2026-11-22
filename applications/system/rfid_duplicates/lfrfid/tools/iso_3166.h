#pragma once

#include <storage/storage.h>
#include <furi.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool iso_3166_get_two_letter(Storage* storage, uint16_t country_code, FuriString* out_two_letter);

bool iso_3166_get_three_letter(
    Storage* storage,
    uint16_t country_code,
    FuriString* out_three_letter);

bool iso_3166_get_full_name(Storage* storage, uint16_t country_code, FuriString* out_full_name);

#ifdef __cplusplus
}
#endif
