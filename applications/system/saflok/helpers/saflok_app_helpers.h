#ifndef _SAFLOK_APP_HELPERS_H_
#define _SAFLOK_APP_HELPERS_H_

#include "saflok_app_i.h"
#include <furi.h>
#include <furi/core/string.h>
#include <nfc/protocols/mf_classic/mf_classic.h>

uint32_t calculate_mf_classic_checksum(const MfClassicData* mf_data);

bool saflok_validate_checksum(SaflokData* data);
void saflok_update_checksum(SaflokApp* app);

bool saflok_unpack_data(uint8_t* decoded_data, SaflokApp* app);
void saflok_update_nfc_device(SaflokApp* app);

void format_field_value(SaflokApp* app, SaflokData* data, uint8_t index, char* buffer, size_t buffer_size);
#endif

