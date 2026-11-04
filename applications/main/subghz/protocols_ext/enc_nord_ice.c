/* enc_nord_ice.fal : SD encoder for nord_ice (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_free enc_nord_ice__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_nord_ice__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_nord_ice__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_nord_ice_alloc enc_nord_ice__subghz_protocol_encoder_nord_ice_alloc
#define subghz_protocol_encoder_nord_ice_deserialize enc_nord_ice__subghz_protocol_encoder_nord_ice_deserialize
#define subghz_protocol_nord_ice enc_nord_ice__subghz_protocol_nord_ice
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/nord_ice.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

#define NORD_ICE_UPLOAD_SIZE 138

struct SubGhzProtocolEncoderNordIce {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_nord_ice_const = {
    .te_short = 300,
    .te_long = 800,
    .te_delta = 150,
    .min_count_bit_for_found = 33,
};

static bool subghz_protocol_encoder_nord_ice_get_upload(SubGhzProtocolEncoderNordIce* instance);

static bool subghz_protocol_encoder_nord_ice_get_upload(SubGhzProtocolEncoderNordIce* instance) {
    furi_assert(instance);
    size_t index = 0;
    uint32_t te_short = subghz_protocol_nord_ice_const.te_short;
    uint32_t te_long = subghz_protocol_nord_ice_const.te_long;
    uint32_t gap = te_short * 25;

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        bool bit = bit_read(instance->generic.data, i - 1);
        if(bit) {
            instance->encoder.upload[index++] = level_duration_make(true, te_long);
            instance->encoder.upload[index++] =
                level_duration_make(false, (i == 1) ? gap : te_short);
        } else {
            instance->encoder.upload[index++] = level_duration_make(true, te_short);
            instance->encoder.upload[index++] =
                level_duration_make(false, (i == 1) ? gap : te_long);
        }
    }

    instance->encoder.size_upload = index;
    return true;
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

/* vendored from lib/subghz/protocols/common.c (not exported) */
LevelDuration subghz_protocol_encoder_common_yield(void* context) {
    SubGhzProtocolEncoderCommon* instance = context;

    if(instance->encoder.repeat == 0 || !instance->encoder.is_running) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }

    LevelDuration ret = instance->encoder.upload[instance->encoder.front];

    if(++instance->encoder.front == instance->encoder.size_upload) {
        if(!subghz_block_generic_global.endless_tx) instance->encoder.repeat--;
        instance->encoder.front = 0;
    }

    return ret;
}

void* subghz_protocol_encoder_nord_ice_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderNordIce* instance = malloc(sizeof(SubGhzProtocolEncoderNordIce));

    instance->base.protocol = &subghz_protocol_nord_ice;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 10;
    instance->encoder.size_upload = NORD_ICE_UPLOAD_SIZE;
    instance->encoder.upload = malloc(NORD_ICE_UPLOAD_SIZE * sizeof(LevelDuration));
    instance->encoder.front = 0;
    instance->encoder.is_running = false;
    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_nord_ice_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderNordIce* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_nord_ice_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) break;

        if(!subghz_protocol_encoder_nord_ice_get_upload(instance)) {
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }
        instance->encoder.front = 0;
        instance->encoder.is_running = true;
    } while(false);
    return ret;
}

static const SubGhzProtocolEncoder enc_nord_ice_vt = {
    .alloc = subghz_protocol_encoder_nord_ice_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_nord_ice_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_nord_ice = {
    .name = SUBGHZ_PROTOCOL_NORD_ICE_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_nord_ice_vt,
};

static const SubGhzEncodePluginEntry enc_nord_ice_entry = {.name = SUBGHZ_PROTOCOL_NORD_ICE_NAME, .encoder = &enc_nord_ice_vt};
static const FlipperAppPluginDescriptor enc_nord_ice_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_nord_ice_entry};
const FlipperAppPluginDescriptor* enc_nord_ice_ep(void) { return &enc_nord_ice_descriptor; }
