

#pragma once

#include "foxdeflock_app.h"

#ifdef __cplusplus
extern "C" {
#endif

void foxdeflock_send_probe(FoxDeFlockApp* app);
void foxdeflock_check_drain(FoxDeFlockApp* app);
void foxdeflock_scan_pump(FoxDeFlockApp* app);
void foxdeflock_scan_drain(FoxDeFlockApp* app);

#ifdef __cplusplus
}
#endif
