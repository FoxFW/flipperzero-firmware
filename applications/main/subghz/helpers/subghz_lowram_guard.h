#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SUBGHZ_LOWRAM_SOFTLOCK_THRESHOLD 20480u

bool subghz_lowram_should_softlock(void);

bool subghz_lowram_mitigate(size_t threshold);

void subghz_lowram_softlock_engage(void);

void subghz_lowram_softlock_release(void);

#ifdef __cplusplus
}
#endif
