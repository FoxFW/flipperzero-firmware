#include "registry.h"

#include "cc1101_int/cc1101_int_interconnect.h"
#include <flipper_application/plugins/plugin_manager.h>
#include <loader/firmware_api/firmware_api.h>

#define TAG "SubGhzDeviceRegistry"

struct SubGhzDeviceRegistry {
    const SubGhzDevice** items;
    size_t size;
    PluginManager* manager;
};

static SubGhzDeviceRegistry* subghz_device_registry = NULL;

static void subghz_device_registry_rebuild_items(SubGhzDeviceRegistry* subghz_device) {
    subghz_device->size = plugin_manager_get_count(subghz_device->manager) + 1;
    free(subghz_device->items);
    subghz_device->items =
        (const SubGhzDevice**)malloc(sizeof(SubGhzDevice*) * subghz_device->size);
    subghz_device->items[0] = &subghz_device_cc1101_int;
    for(uint32_t i = 1; i < subghz_device->size; i++) {
        const SubGhzDevice* plugin = plugin_manager_get_ep(subghz_device->manager, i - 1);
        subghz_device->items[i] = plugin;
    }

    FURI_LOG_I(TAG, "Loaded %zu radio device", subghz_device->size);
}

static SubGhzDeviceRegistry* subghz_device_registry_alloc_manager(void) {
    SubGhzDeviceRegistry* subghz_device =
        (SubGhzDeviceRegistry*)malloc(sizeof(SubGhzDeviceRegistry));
    subghz_device->manager = plugin_manager_alloc(
        SUBGHZ_RADIO_DEVICE_PLUGIN_APP_ID,
        SUBGHZ_RADIO_DEVICE_PLUGIN_API_VERSION,
        firmware_api_interface);
    subghz_device->items = NULL;
    subghz_device->size = 0;
    return subghz_device;
}

void subghz_device_registry_init(void) {
    SubGhzDeviceRegistry* subghz_device = subghz_device_registry_alloc_manager();

    if(plugin_manager_load_all_ex(subghz_device->manager, EXT_PATH("apps_data/subghz/plugins"), "radio_device_") !=
       PluginManagerErrorNone) {
        FURI_LOG_E(TAG, "Failed to load all libs");
    }

    subghz_device_registry_rebuild_items(subghz_device);
    subghz_device_registry = subghz_device;
}

void subghz_device_registry_init_internal_only(void) {
    SubGhzDeviceRegistry* subghz_device = subghz_device_registry_alloc_manager();
    subghz_device_registry_rebuild_items(subghz_device);
    subghz_device_registry = subghz_device;
}

bool subghz_device_registry_load_external(void) {
    furi_assert(subghz_device_registry);

    bool ok =
        plugin_manager_load_all_ex(subghz_device_registry->manager, EXT_PATH("apps_data/subghz/plugins"), "radio_device_") ==
        PluginManagerErrorNone;
    if(!ok) {
        FURI_LOG_E(TAG, "Failed to load all libs");
    }
    subghz_device_registry_rebuild_items(subghz_device_registry);
    return ok;
}

void subghz_device_registry_deinit(void) {
    plugin_manager_free(subghz_device_registry->manager);
    free(subghz_device_registry->items);
    free(subghz_device_registry);
    subghz_device_registry = NULL;
}

bool subghz_device_registry_is_valid(void) {
    return subghz_device_registry != NULL;
}

const SubGhzDevice* subghz_device_registry_get_by_name(const char* name) {
    furi_assert(subghz_device_registry);

    if(name != NULL) {
        for(size_t i = 0; i < subghz_device_registry->size; i++) {
            if(strcmp(name, subghz_device_registry->items[i]->name) == 0) {
                return subghz_device_registry->items[i];
            }
        }
    }
    return NULL;
}

const SubGhzDevice* subghz_device_registry_get_by_index(size_t index) {
    furi_assert(subghz_device_registry);
    if(index < subghz_device_registry->size) {
        return subghz_device_registry->items[index];
    } else {
        return NULL;
    }
}

size_t subghz_device_registry_count(void) {
    furi_assert(subghz_device_registry);
    return subghz_device_registry->size;
}
