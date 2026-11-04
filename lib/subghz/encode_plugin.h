/* Encoder (TX) externalization for the detect/decode split - FoxFW2.0 v2.1.
 *
 * A protocol whose encoder lives on the SD card points its SubGhzProtocol.encoder
 * at a SubGhzEncoderExternal placeholder. The placeholder's functions are
 * harmless non-NULL stubs, so existing "can this protocol be sent?" checks
 * (protocol->encoder->deserialize != NULL) keep passing.
 *
 * The transmitter recognises the placeholder, loads enc_<x>.fal from
 * /ext/apps_data/subghz/plugins, and drives the plugin's REAL encoder vtable for
 * the lifetime of that transmitter. Because this lives in libsubghz, every app
 * that transmits through it (Sub-GHz, SubGhz Remote, CLI) gets it automatically.
 *
 * A plugin exports one SubGhzEncodePluginEntry via a FlipperAppPluginDescriptor
 * with appid SUBGHZ_ENCODE_PLUGIN_APP_ID. */
#pragma once

#include "protocols/base.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SUBGHZ_ENCODE_PLUGIN_APP_ID      "subghz_encode_plugin"
#define SUBGHZ_ENCODE_PLUGIN_API_VERSION 2

typedef struct {
    const char* name; /* must equal SubGhzProtocol.name of the internal protocol */
    const SubGhzProtocolEncoder* encoder; /* real encoder, implemented in the plugin */
    /* Host bridge (optional). If non-NULL, the loader stores the protocol's host table
     * here before the encoder is used: pointers to firmware functions the plugin may not
     * import (crypto, prog-mode, ...) and to live firmware state (key tables, buffers),
     * so the plugin operates on the REAL state rather than private copies. */
    const void** host_slot;
} SubGhzEncodePluginEntry;

typedef struct {
    SubGhzProtocolEncoder vtable; /* MUST be first: SubGhzProtocol.encoder points here */
    const char* fal; /* plugin file stem, e.g. "enc_came" */
    const void* host; /* host bridge table for this protocol, or NULL */
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

/** True if this encoder is an SD placeholder rather than a real encoder. */
bool subghz_encoder_is_external(const SubGhzProtocolEncoder* encoder);

/** Load the SD encoder plugin for an externalized protocol.
 *  Returns the plugin's real encoder vtable (and a handle to release), or NULL
 *  if the protocol is not external or the plugin can't be loaded/validated. */
const SubGhzProtocolEncoder*
    subghz_encode_plugin_acquire(const SubGhzProtocol* protocol, void** handle);

/** Unload a plugin returned by subghz_encode_plugin_acquire. NULL-safe. */
void subghz_encode_plugin_release(void* handle);

#ifdef __cplusplus
}
#endif
