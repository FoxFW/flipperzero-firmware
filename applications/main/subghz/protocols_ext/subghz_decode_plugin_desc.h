#pragma once

#include <lib/flipper_application/flipper_application.h>
#include <lib/subghz/protocols/decode_plugin.h>

#define SUBGHZ_DECODE_PLUGIN_APP_ID      "subghz_decode_plugin"
#define SUBGHZ_DECODE_PLUGIN_API_VERSION 1U

typedef void (*SubGhzDecodeGetStringFn)(const SubGhzDecodeInput* input, FuriString* output);

typedef struct {
    const char* name;
    SubGhzDecodeGetStringFn get_string;
} SubGhzDecodeEntry;

typedef struct {
    const SubGhzDecodeEntry* items;
    size_t size;
} SubGhzDecodeRegistry;
