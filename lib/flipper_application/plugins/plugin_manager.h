#pragma once

#include <flipper_application/flipper_application.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PluginManager PluginManager;

typedef enum {
    PluginManagerErrorNone = 0,
    PluginManagerErrorLoaderError,
    PluginManagerErrorApplicationIdMismatch,
    PluginManagerErrorAPIVersionMismatch,
} PluginManagerError;

PluginManager* plugin_manager_alloc(
    const char* application_id,
    uint32_t api_version,
    const ElfApiInterface* api_interface);

void plugin_manager_free(PluginManager* manager);

PluginManagerError plugin_manager_load_single(PluginManager* manager, const char* path);

PluginManagerError plugin_manager_load_all(PluginManager* manager, const char* path);

/* Like plugin_manager_load_all but only loads .fal whose filename starts with
 * name_prefix (NULL = all). Lets one plugins dir hold several plugin families
 * without one family preloading another's. FoxFW2.0 2026-10-19. */
PluginManagerError plugin_manager_load_all_ex(
    PluginManager* manager,
    const char* path,
    const char* name_prefix);

uint32_t plugin_manager_get_count(PluginManager* manager);

const FlipperAppPluginDescriptor* plugin_manager_get(PluginManager* manager, uint32_t index);

const void* plugin_manager_get_ep(PluginManager* manager, uint32_t index);

#ifdef __cplusplus
}
#endif
