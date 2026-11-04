/* SD encoder-plugin loader used by the transmitter - FoxFW2.0 v2.1. See encode_plugin.h.
 * Load-per-transmitter (no persistent cache): no heap is held once TX ends, which
 * matters on a device that has hit OOM before. Plugins are small, so load is quick. */
#include "encode_plugin.h"

#include <furi.h>
#include <string.h>
#include <storage/storage.h>
#include <flipper_application/plugins/plugin_manager.h>
#include <loader/firmware_api/firmware_api.h>

#define TAG "SubGhzEncodePlugin"
#define SUBGHZ_ENCODE_PLUGIN_DIR EXT_PATH("apps_data/subghz/plugins")

/* Placeholder stubs. Never used for real TX (the transmitter swaps in the plugin's
 * encoder); they only need to be non-NULL and safe if something calls them. */
void* subghz_encoder_external_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return NULL;
}

void subghz_encoder_external_free(void* context) {
    UNUSED(context);
}

SubGhzProtocolStatus
    subghz_encoder_external_deserialize(void* context, FlipperFormat* flipper_format) {
    UNUSED(context);
    UNUSED(flipper_format);
    return SubGhzProtocolStatusError;
}

void subghz_encoder_external_stop(void* context) {
    UNUSED(context);
}

LevelDuration subghz_encoder_external_yield(void* context) {
    UNUSED(context);
    return level_duration_reset();
}

bool subghz_encoder_is_external(const SubGhzProtocolEncoder* encoder) {
    return encoder && encoder->alloc == subghz_encoder_external_alloc;
}

const SubGhzProtocolEncoder*
    subghz_encode_plugin_acquire(const SubGhzProtocol* protocol, void** handle) {
    furi_check(handle);
    *handle = NULL;
    if(!protocol || !protocol->name || !subghz_encoder_is_external(protocol->encoder)) {
        return NULL;
    }
    const SubGhzEncoderExternal* ext = (const SubGhzEncoderExternal*)protocol->encoder;
    if(!ext->fal) return NULL;

    PluginManager* manager = plugin_manager_alloc(
        SUBGHZ_ENCODE_PLUGIN_APP_ID, SUBGHZ_ENCODE_PLUGIN_API_VERSION, firmware_api_interface);
    FuriString* path = furi_string_alloc_printf("%s/%s.fal", SUBGHZ_ENCODE_PLUGIN_DIR, ext->fal);
    PluginManagerError err = plugin_manager_load_single(manager, furi_string_get_cstr(path));
    furi_string_free(path);
    if(err != PluginManagerErrorNone) {
        FURI_LOG_E(TAG, "%s: load failed (%d)", ext->fal, (int)err);
        plugin_manager_free(manager);
        return NULL;
    }

    const SubGhzEncodePluginEntry* entry = plugin_manager_get_ep(manager, 0);
    if(!entry || !entry->name || !entry->encoder || !entry->encoder->alloc ||
       !entry->encoder->free || !entry->encoder->deserialize || !entry->encoder->yield ||
       strcmp(entry->name, protocol->name) != 0) {
        FURI_LOG_E(TAG, "%s: invalid plugin entry", ext->fal);
        plugin_manager_free(manager);
        return NULL;
    }

    if(entry->host_slot) {
        if(!ext->host) {
            FURI_LOG_E(TAG, "%s: plugin needs a host bridge, protocol provides none", ext->fal);
            plugin_manager_free(manager);
            return NULL;
        }
        *entry->host_slot = ext->host; /* plugin RAM; set before any encoder call */
    }

    *handle = manager;
    return entry->encoder;
}

void subghz_encode_plugin_release(void* handle) {
    if(handle) plugin_manager_free((PluginManager*)handle);
}
