/* enc_opel_v0.fal : SD encoder for opel_v0 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_free enc_opel_v0__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_opel_v0__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_opel_v0_alloc enc_opel_v0__subghz_protocol_encoder_opel_v0_alloc
#define subghz_protocol_encoder_opel_v0_deserialize enc_opel_v0__subghz_protocol_encoder_opel_v0_deserialize
#define subghz_protocol_encoder_opel_v0_yield enc_opel_v0__subghz_protocol_encoder_opel_v0_yield
#define subghz_protocol_opel_v0 enc_opel_v0__subghz_protocol_opel_v0
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/opel_v0.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

struct SubGhzProtocolEncoderOpelV0 {
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
// Encoder stubs (no encoding yet)
// ============================================================================

void* subghz_protocol_encoder_opel_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderOpelV0* instance = malloc(sizeof(SubGhzProtocolEncoderOpelV0));
    instance->base.protocol = &subghz_protocol_opel_v0;
    instance->generic.protocol_name = subghz_protocol_opel_v0.name;
    instance->encoder.repeat = 1;
    instance->encoder.size_upload = 2;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_opel_v0_deserialize(void* context, FlipperFormat* flipper_format) {
    UNUSED(context);
    UNUSED(flipper_format);
    return SubGhzProtocolStatusErrorEncoderGetUpload;
}

LevelDuration subghz_protocol_encoder_opel_v0_yield(void* context) {
    SubGhzProtocolEncoderOpelV0* instance = context;
    if(!instance->encoder.is_running) {
        return level_duration_reset();
    }
    return level_duration_reset();
}

static const SubGhzProtocolEncoder enc_opel_v0_vt = {
    .alloc = subghz_protocol_encoder_opel_v0_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_opel_v0_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_opel_v0_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_opel_v0 = {
    .name = SUBGHZ_PROTOCOL_OPEL_V0_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_opel_v0_vt,
};

static const SubGhzEncodePluginEntry enc_opel_v0_entry = {.name = SUBGHZ_PROTOCOL_OPEL_V0_NAME, .encoder = &enc_opel_v0_vt};
static const FlipperAppPluginDescriptor enc_opel_v0_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_opel_v0_entry};
const FlipperAppPluginDescriptor* enc_opel_v0_ep(void) { return &enc_opel_v0_descriptor; }
