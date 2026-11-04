/* enc_compustar.fal : SD encoder for compustar (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_compustar enc_compustar__subghz_protocol_compustar
#define subghz_protocol_encoder_common_free enc_compustar__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_compustar__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_compustar_alloc enc_compustar__subghz_protocol_encoder_compustar_alloc
#define subghz_protocol_encoder_compustar_deserialize enc_compustar__subghz_protocol_encoder_compustar_deserialize
#define subghz_protocol_encoder_compustar_yield enc_compustar__subghz_protocol_encoder_compustar_yield
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/compustar.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

struct SubGhzProtocolEncoderCompustar {
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
// Encoder stubs (fixed pulse widths, but the PWM gap timing is not published
// by rtl_433 — TX would be guesswork, so no encoder is provided)
// ============================================================================

void* subghz_protocol_encoder_compustar_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderCompustar* instance = malloc(sizeof(SubGhzProtocolEncoderCompustar));
    instance->base.protocol = &subghz_protocol_compustar;
    instance->generic.protocol_name = subghz_protocol_compustar.name;
    instance->encoder.repeat = 1;
    instance->encoder.size_upload = 2;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_compustar_deserialize(void* context, FlipperFormat* flipper_format) {
    UNUSED(context);
    UNUSED(flipper_format);
    return SubGhzProtocolStatusErrorEncoderGetUpload;
}

LevelDuration subghz_protocol_encoder_compustar_yield(void* context) {
    SubGhzProtocolEncoderCompustar* instance = context;
    if(!instance->encoder.is_running) {
        return level_duration_reset();
    }
    return level_duration_reset();
}

static const SubGhzProtocolEncoder enc_compustar_vt = {
    .alloc = subghz_protocol_encoder_compustar_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_compustar_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_compustar_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_compustar = {
    .name = COMPUSTAR_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_compustar_vt,
};

static const SubGhzEncodePluginEntry enc_compustar_entry = {.name = COMPUSTAR_PROTOCOL_NAME, .encoder = &enc_compustar_vt};
static const FlipperAppPluginDescriptor enc_compustar_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_compustar_entry};
const FlipperAppPluginDescriptor* enc_compustar_ep(void) { return &enc_compustar_descriptor; }
