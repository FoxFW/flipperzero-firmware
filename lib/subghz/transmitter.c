#include "transmitter.h"

#include "protocols/base.h"
#include "registry.h"
#include "encode_plugin.h"
#include "protocols/raw.h"
#include <string.h>

struct SubGhzTransmitter {
    const SubGhzProtocol* protocol;
    /* Encoder actually driven: the protocol's own, or the SD plugin's real encoder
     * when the protocol's encoder is an external placeholder (FoxFW2.0 v2.1). */
    const SubGhzProtocolEncoder* encoder;
    void* encode_plugin; /* plugin handle while loaded, else NULL */
    SubGhzProtocolEncoderBase* protocol_instance;
};

SubGhzTransmitter*
    subghz_transmitter_alloc_init(SubGhzEnvironment* environment, const char* protocol_name) {
    SubGhzTransmitter* instance = NULL;
    const SubGhzProtocolRegistry* protocol_registry_items =
        subghz_environment_get_protocol_registry(environment);

    const SubGhzProtocol* protocol =
        subghz_protocol_registry_get_by_name(protocol_registry_items, protocol_name);

    if(protocol && protocol->encoder) {
        const SubGhzProtocolEncoder* encoder = protocol->encoder;
        void* plugin = NULL;
        if(subghz_encoder_is_external(encoder)) {
            encoder = subghz_encode_plugin_acquire(protocol, &plugin);
        }
        if(encoder && encoder->alloc) {
            instance = malloc(sizeof(SubGhzTransmitter));
            instance->protocol = protocol;
            instance->encoder = encoder;
            instance->encode_plugin = plugin;
            instance->protocol_instance = encoder->alloc(environment);
        } else {
            subghz_encode_plugin_release(plugin);
        }
    }
    return instance;
}

void subghz_transmitter_free(SubGhzTransmitter* instance) {
    furi_check(instance);
    if(instance->encoder && instance->encoder->free) {
        instance->encoder->free(instance->protocol_instance);
    }
    /* Unload only after the plugin's own free() has run. */
    subghz_encode_plugin_release(instance->encode_plugin);
    free(instance);
}

SubGhzProtocolEncoderBase* subghz_transmitter_get_protocol_instance(SubGhzTransmitter* instance) {
    furi_check(instance);
    return instance->protocol_instance;
}

uint8_t subghz_transmitter_get_raw_progress(SubGhzTransmitter* instance) {
    furi_check(instance);
    if(instance->protocol && instance->protocol->name &&
       strcmp(instance->protocol->name, SUBGHZ_PROTOCOL_RAW_NAME) == 0 &&
       instance->protocol_instance) {
        return subghz_protocol_encoder_raw_get_progress(instance->protocol_instance);
    }
    return 255;
}

bool subghz_transmitter_stop(SubGhzTransmitter* instance) {
    furi_check(instance);
    bool ret = false;
    if(instance->encoder && instance->encoder->stop) {
        instance->encoder->stop(instance->protocol_instance);
        ret = true;
    }
    return ret;
}

SubGhzProtocolStatus
    subghz_transmitter_deserialize(SubGhzTransmitter* instance, FlipperFormat* flipper_format) {
    furi_check(instance);
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    if(instance->encoder && instance->encoder->deserialize) {
        ret = instance->encoder->deserialize(instance->protocol_instance, flipper_format);
    }
    return ret;
}

LevelDuration subghz_transmitter_yield(void* context) {
    SubGhzTransmitter* instance = context;

    if(!instance || !instance->encoder || !instance->encoder->yield) {
        return level_duration_reset();
    }
    return instance->encoder->yield(instance->protocol_instance);
}
