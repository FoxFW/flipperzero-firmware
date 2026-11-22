#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

int32_t fox_subghz_channel(const char* args);

bool fox_subghz_external_present(void);

size_t fox_subghz_protocol_count(void);
