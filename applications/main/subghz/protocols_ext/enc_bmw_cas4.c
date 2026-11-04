/* enc_bmw_cas4.fal : SD encoder for bmw_cas4 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_bmw_cas4 enc_bmw_cas4__subghz_protocol_bmw_cas4
#define subghz_protocol_encoder_bmw_cas4_alloc enc_bmw_cas4__subghz_protocol_encoder_bmw_cas4_alloc
#define subghz_protocol_encoder_bmw_cas4_deserialize enc_bmw_cas4__subghz_protocol_encoder_bmw_cas4_deserialize
#define subghz_protocol_encoder_bmw_cas4_yield enc_bmw_cas4__subghz_protocol_encoder_bmw_cas4_yield
#define subghz_protocol_encoder_common_free enc_bmw_cas4__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_bmw_cas4__subghz_protocol_encoder_common_stop
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/bmw_cas4.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/subghz/protocols/common.h>

struct SubGhzProtocolEncoderBmwCas4 {
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

void* subghz_protocol_encoder_bmw_cas4_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderBmwCas4* instance = calloc(1, sizeof(SubGhzProtocolEncoderBmwCas4));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_bmw_cas4;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.is_running = false;
    instance->encoder.size_upload = 1;
    instance->encoder.upload = malloc(sizeof(LevelDuration));
    furi_check(instance->encoder.upload);
    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_bmw_cas4_deserialize(void* context, FlipperFormat* flipper_format) {
    UNUSED(context);
    UNUSED(flipper_format);
    return SubGhzProtocolStatusError;
}

LevelDuration subghz_protocol_encoder_bmw_cas4_yield(void* context) {
    UNUSED(context);
    return level_duration_reset();
}

static const SubGhzProtocolEncoder enc_bmw_cas4_vt = {
    .alloc = subghz_protocol_encoder_bmw_cas4_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_bmw_cas4_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_bmw_cas4_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_bmw_cas4 = {
    .name = BMW_CAS4_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,
    .decoder = NULL,
    .encoder = &enc_bmw_cas4_vt,
};

static const SubGhzEncodePluginEntry enc_bmw_cas4_entry = {.name = BMW_CAS4_PROTOCOL_NAME, .encoder = &enc_bmw_cas4_vt};
static const FlipperAppPluginDescriptor enc_bmw_cas4_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_bmw_cas4_entry};
const FlipperAppPluginDescriptor* enc_bmw_cas4_ep(void) { return &enc_bmw_cas4_descriptor; }
