#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <lib/flipper_format/flipper_format.h>
#include "furi.h"
#include "furi_hal.h"
#include <lib/subghz/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PCSGBlockGeneric PCSGBlockGeneric;

struct PCSGBlockGeneric {
    const char* protocol_name;
    FuriString* result_ric;
    FuriString* result_msg;
};

void pcsg_block_generic_get_preset_name(const char* preset_name, FuriString* preset_str);

SubGhzProtocolStatus pcsg_block_generic_serialize(
    PCSGBlockGeneric* instance,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    pcsg_block_generic_deserialize(PCSGBlockGeneric* instance, FlipperFormat* flipper_format);

float pcsg_block_generic_fahrenheit_to_celsius(float fahrenheit);

#ifdef __cplusplus
}
#endif
