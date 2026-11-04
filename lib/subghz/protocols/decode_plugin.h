#pragma once

#include "../blocks/generic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SUBGHZ_DECODE_EXTRA_MAX 8

typedef struct {
    void* decoder;
    SubGhzBlockGeneric* generic;
    const uint64_t* extra;
    size_t extra_count;
    const char* aux;
} SubGhzDecodeInput;

typedef bool (
    *SubGhzDecodeLoaderFn)(const char* fal_base, const SubGhzDecodeInput* input, FuriString* output);

void subghz_decode_plugin_set_loader(SubGhzDecodeLoaderFn loader);

bool subghz_decode_plugin_get_string(
    const char* fal_base,
    const SubGhzDecodeInput* input,
    FuriString* output);

#ifdef __cplusplus
}
#endif
