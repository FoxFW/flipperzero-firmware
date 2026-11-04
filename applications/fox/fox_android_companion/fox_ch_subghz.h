#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// SubGHz RX/TX channel. args: "RX <freqHz> <dev> <ms> <outfile> <preset>" or
// "TX <file> <freqHz> <dev> <preset>". Returns 0 on success, negative on error.
int32_t fox_subghz_channel(const char* args);

// True if an external CC1101 radio module is attached.
bool fox_subghz_external_present(void);

// Number of sub-GHz protocols compiled into this firmware's registry (live count).
size_t fox_subghz_protocol_count(void);
