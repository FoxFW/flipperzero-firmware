#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROTOPIRATE_LOWRAM_THRESHOLD 20480u

bool protopirate_lowram_mitigate(size_t threshold);
void protopirate_lowram_softlock_engage(void);
void protopirate_lowram_softlock_release(void);

#ifdef __cplusplus
}
#endif
