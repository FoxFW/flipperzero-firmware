
#pragma once

#include "protocols/base.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SUBGHZ_ENCODE_PLUGIN_APP_ID      "subghz_encode_plugin"
#define SUBGHZ_ENCODE_PLUGIN_API_VERSION 2

typedef struct {
    const char* name;
    const SubGhzProtocolEncoder* encoder;

    const void** host_slot;
} SubGhzEncodePluginEntry;

typedef struct {
    SubGhzProtocolEncoder vtable;
    const char* fal;
    const void* host;
} SubGhzEncoderExternal;

void* subghz_encoder_external_alloc(SubGhzEnvironment* environment);
void subghz_encoder_external_free(void* context);
SubGhzProtocolStatus
    subghz_encoder_external_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_encoder_external_stop(void* context);
LevelDuration subghz_encoder_external_yield(void* context);

#define SUBGHZ_ENCODER_EXTERNAL_HOST(fal_stem, host_table)      \
    {                                                            \
        .vtable =                                                \
            {                                                    \
                .alloc = subghz_encoder_external_alloc,          \
                .free = subghz_encoder_external_free,            \
                .deserialize = subghz_encoder_external_deserialize, \
                .stop = subghz_encoder_external_stop,            \
                .yield = subghz_encoder_external_yield,          \
            },                                                   \
        .fal = (fal_stem), .host = (host_table),                 \
    }
#define SUBGHZ_ENCODER_EXTERNAL(fal_stem) SUBGHZ_ENCODER_EXTERNAL_HOST(fal_stem, NULL)

bool subghz_encoder_is_external(const SubGhzProtocolEncoder* encoder);

const SubGhzProtocolEncoder*
    subghz_encode_plugin_acquire(const SubGhzProtocol* protocol, void** handle);

void subghz_encode_plugin_release(void* handle);

#ifdef __cplusplus
}
#endif
