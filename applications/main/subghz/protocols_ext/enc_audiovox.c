/* enc_audiovox.fal : SD encoder for audiovox (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_audiovox enc_audiovox__subghz_protocol_audiovox
#define subghz_protocol_encoder_common_free enc_audiovox__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_audiovox__subghz_protocol_encoder_common_stop
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/audiovox.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

struct SubGhzProtocolEncoderAudiovox {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

typedef struct SubGhzProtocolEncoderAudiovox SubGhzProtocolEncoderAudiovox;

// ============================================================================
// Encoder stubs (fixed-code remote; TX not implemented — load/save only)
// ============================================================================

static void* subghz_protocol_encoder_audiovox_alloc(SubGhzEnvironment* environment);
static SubGhzProtocolStatus
    subghz_protocol_encoder_audiovox_deserialize(void* context, FlipperFormat* flipper_format);
static LevelDuration subghz_protocol_encoder_audiovox_yield(void* context);

// ============================================================================
// Encoder stubs (fixed-code remote; TX not implemented — load/save only)
// ============================================================================

static void* subghz_protocol_encoder_audiovox_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderAudiovox* instance = malloc(sizeof(SubGhzProtocolEncoderAudiovox));
    instance->base.protocol = &subghz_protocol_audiovox;
    instance->generic.protocol_name = subghz_protocol_audiovox.name;
    instance->encoder.repeat = 1;
    instance->encoder.size_upload = 2;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

static SubGhzProtocolStatus
    subghz_protocol_encoder_audiovox_deserialize(void* context, FlipperFormat* flipper_format) {
    UNUSED(context);
    UNUSED(flipper_format);
    return SubGhzProtocolStatusErrorEncoderGetUpload;
}

static LevelDuration subghz_protocol_encoder_audiovox_yield(void* context) {
    SubGhzProtocolEncoderAudiovox* instance = context;
    if(!instance->encoder.is_running) {
        return level_duration_reset();
    }
    return level_duration_reset();
}

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

static const SubGhzProtocolEncoder enc_audiovox_vt = {
    .alloc = subghz_protocol_encoder_audiovox_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_audiovox_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_audiovox_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_audiovox = {
    .name = AUDIOVOX_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_audiovox_vt,
};

static const SubGhzEncodePluginEntry enc_audiovox_entry = {.name = AUDIOVOX_PROTOCOL_NAME, .encoder = &enc_audiovox_vt};
static const FlipperAppPluginDescriptor enc_audiovox_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_audiovox_entry};
const FlipperAppPluginDescriptor* enc_audiovox_ep(void) { return &enc_audiovox_descriptor; }
