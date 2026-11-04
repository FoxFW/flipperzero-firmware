/* enc_continental_rke.fal : SD encoder for continental_rke (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_continental_rke enc_continental_rke__subghz_protocol_continental_rke
#define subghz_protocol_encoder_common_free enc_continental_rke__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_continental_rke__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_continental_rke_alloc enc_continental_rke__subghz_protocol_encoder_continental_rke_alloc
#define subghz_protocol_encoder_continental_rke_deserialize enc_continental_rke__subghz_protocol_encoder_continental_rke_deserialize
#define subghz_protocol_encoder_continental_rke_yield enc_continental_rke__subghz_protocol_encoder_continental_rke_yield
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/continental_rke.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

struct SubGhzProtocolEncoderContinentalRke {
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
// Encoder stubs (rolling code / encrypted — no TX)
// ============================================================================

void* subghz_protocol_encoder_continental_rke_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderContinentalRke* instance =
        malloc(sizeof(SubGhzProtocolEncoderContinentalRke));
    instance->base.protocol = &subghz_protocol_continental_rke;
    instance->generic.protocol_name = subghz_protocol_continental_rke.name;
    instance->encoder.repeat = 1;
    instance->encoder.size_upload = 2;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

SubGhzProtocolStatus subghz_protocol_encoder_continental_rke_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    UNUSED(context);
    UNUSED(flipper_format);
    return SubGhzProtocolStatusErrorEncoderGetUpload;
}

LevelDuration subghz_protocol_encoder_continental_rke_yield(void* context) {
    SubGhzProtocolEncoderContinentalRke* instance = context;
    if(!instance->encoder.is_running) {
        return level_duration_reset();
    }
    return level_duration_reset();
}

static const SubGhzProtocolEncoder enc_continental_rke_vt = {
    .alloc = subghz_protocol_encoder_continental_rke_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_continental_rke_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_continental_rke_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_continental_rke = {
    .name = CONTINENTAL_RKE_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_continental_rke_vt,
};

static const SubGhzEncodePluginEntry enc_continental_rke_entry = {.name = CONTINENTAL_RKE_PROTOCOL_NAME, .encoder = &enc_continental_rke_vt};
static const FlipperAppPluginDescriptor enc_continental_rke_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_continental_rke_entry};
const FlipperAppPluginDescriptor* enc_continental_rke_ep(void) { return &enc_continental_rke_descriptor; }
