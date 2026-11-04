/* enc_gm_rke.fal : SD encoder for gm_rke (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_free enc_gm_rke__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_gm_rke__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_gm_rke_alloc enc_gm_rke__subghz_protocol_encoder_gm_rke_alloc
#define subghz_protocol_encoder_gm_rke_deserialize enc_gm_rke__subghz_protocol_encoder_gm_rke_deserialize
#define subghz_protocol_encoder_gm_rke_yield enc_gm_rke__subghz_protocol_encoder_gm_rke_yield
#define subghz_protocol_gm_rke enc_gm_rke__subghz_protocol_gm_rke
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/gm_rke.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

struct SubGhzProtocolEncoderGmRke {
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
// Encoder stubs (no TX: rolling code + encrypted field, not replayable)
// ============================================================================

void* subghz_protocol_encoder_gm_rke_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderGmRke* instance = malloc(sizeof(SubGhzProtocolEncoderGmRke));
    instance->base.protocol = &subghz_protocol_gm_rke;
    instance->generic.protocol_name = subghz_protocol_gm_rke.name;
    instance->encoder.repeat = 1;
    instance->encoder.size_upload = 2;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_gm_rke_deserialize(void* context, FlipperFormat* flipper_format) {
    UNUSED(context);
    UNUSED(flipper_format);
    return SubGhzProtocolStatusErrorEncoderGetUpload;
}

LevelDuration subghz_protocol_encoder_gm_rke_yield(void* context) {
    SubGhzProtocolEncoderGmRke* instance = context;
    if(!instance->encoder.is_running) {
        return level_duration_reset();
    }
    return level_duration_reset();
}

static const SubGhzProtocolEncoder enc_gm_rke_vt = {
    .alloc = subghz_protocol_encoder_gm_rke_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_gm_rke_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_gm_rke_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_gm_rke = {
    .name = GM_RKE_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_gm_rke_vt,
};

static const SubGhzEncodePluginEntry enc_gm_rke_entry = {.name = GM_RKE_PROTOCOL_NAME, .encoder = &enc_gm_rke_vt};
static const FlipperAppPluginDescriptor enc_gm_rke_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_gm_rke_entry};
const FlipperAppPluginDescriptor* enc_gm_rke_ep(void) { return &enc_gm_rke_descriptor; }
