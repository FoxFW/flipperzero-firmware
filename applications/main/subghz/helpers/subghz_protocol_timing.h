#pragma once

#include <furi.h>

typedef struct {
    const char* name;
    uint16_t te_short;
    uint16_t te_long;
} SubGhzProtocolTiming;

extern const SubGhzProtocolTiming subghz_protocol_timing[];
extern const size_t subghz_protocol_timing_count;
