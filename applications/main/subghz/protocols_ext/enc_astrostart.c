/* enc_astrostart.fal : SD encoder for astrostart (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_astrostart enc_astrostart__subghz_protocol_astrostart
#define subghz_protocol_encoder_astrostart_alloc enc_astrostart__subghz_protocol_encoder_astrostart_alloc
#define subghz_protocol_encoder_astrostart_deserialize enc_astrostart__subghz_protocol_encoder_astrostart_deserialize
#define subghz_protocol_encoder_astrostart_yield enc_astrostart__subghz_protocol_encoder_astrostart_yield
#define subghz_protocol_encoder_common_free enc_astrostart__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_astrostart__subghz_protocol_encoder_common_stop
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/astrostart.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

struct SubGhzProtocolEncoderAstrostart {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

/* vendored from lib/subghz/protocols/common.c (not exported) */
void subghz_protocol_encoder_common_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderCommon* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

/* vendored from lib/subghz/protocols/common.c (not exported) */
void subghz_protocol_encoder_common_stop(void* context) {
    SubGhzProtocolEncoderCommon* instance = context;
    instance->encoder.is_running = false;
}

// ============================================================================
// Encoder stub (no TX — rolling button set, replay only)
// ============================================================================

void* subghz_protocol_encoder_astrostart_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderAstrostart* instance =
        malloc(sizeof(SubGhzProtocolEncoderAstrostart));
    instance->base.protocol = &subghz_protocol_astrostart;
    instance->generic.protocol_name = subghz_protocol_astrostart.name;
    instance->encoder.repeat = 1;
    instance->encoder.size_upload = 2;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_astrostart_deserialize(void* context, FlipperFormat* flipper_format) {
    UNUSED(context);
    UNUSED(flipper_format);
    return SubGhzProtocolStatusErrorEncoderGetUpload;
}

LevelDuration subghz_protocol_encoder_astrostart_yield(void* context) {
    SubGhzProtocolEncoderAstrostart* instance = context;
    if(!instance->encoder.is_running) {
        return level_duration_reset();
    }
    return level_duration_reset();
}

static const SubGhzProtocolEncoder enc_astrostart_vt = {
    .alloc = subghz_protocol_encoder_astrostart_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_astrostart_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_astrostart_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_astrostart = {
    .name = ASTROSTART_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_astrostart_vt,
};

static const SubGhzEncodePluginEntry enc_astrostart_entry = {.name = ASTROSTART_PROTOCOL_NAME, .encoder = &enc_astrostart_vt};
static const FlipperAppPluginDescriptor enc_astrostart_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_astrostart_entry};
const FlipperAppPluginDescriptor* enc_astrostart_ep(void) { return &enc_astrostart_descriptor; }
