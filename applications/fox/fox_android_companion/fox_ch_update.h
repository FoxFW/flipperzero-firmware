#pragma once

#include <stdint.h>

// Flipper-side firmware install: extract an on-SD update .tar and hand off to updater_app (which
// flashes + reboots). arg is the .tar path on the SD. Returns 0 on handoff, negative on error.
int32_t fox_update_channel(const char* tar_path);
