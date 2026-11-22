

#define subghz_protocol_encoder_common_free enc_freezer__common_free
#define subghz_protocol_encoder_common_stop enc_freezer__common_stop
#define subghz_protocol_encoder_common_yield enc_freezer__common_yield
#define subghz_protocol_encoder_freezer_alloc enc_freezer__alloc
#define subghz_protocol_encoder_freezer_deserialize enc_freezer__deserialize
#define subghz_protocol_freezer enc_freezer__subghz_protocol_freezer

#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/freezer.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

#define FRZ_TE_SHORT 250U
#define FRZ_TE_LONG  500U
#define FRZ_GAP_US   9000U

struct SubGhzProtocolEncoderFreezer {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static void subghz_protocol_encoder_freezer_get_upload(struct SubGhzProtocolEncoderFreezer* instance) {
    furi_assert(instance);
    size_t index = 0;
    uint64_t data = instance->generic.data;
    uint8_t bits = instance->generic.data_count_bit;
    instance->encoder.upload[index++] = level_duration_make(true, FRZ_TE_SHORT);
    for(uint8_t i = bits; i > 0; i--) {
        if((data >> (i - 1)) & 1U) {
            instance->encoder.upload[index++] = level_duration_make(false, FRZ_TE_SHORT);
            instance->encoder.upload[index++] = level_duration_make(true, FRZ_TE_LONG);
        } else {
            instance->encoder.upload[index++] = level_duration_make(false, FRZ_TE_LONG);
            instance->encoder.upload[index++] = level_duration_make(true, FRZ_TE_SHORT);
        }
    }
    instance->encoder.upload[index++] = level_duration_make(false, FRZ_GAP_US);
    instance->encoder.size_upload = index;
    instance->encoder.front = 0;
}

void subghz_protocol_encoder_common_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderCommon* instance = context;
    free(instance->encoder.upload);
    free(instance);
}
void subghz_protocol_encoder_common_stop(void* context) {
    SubGhzProtocolEncoderCommon* instance = context;
    instance->encoder.is_running = false;
}
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

void* subghz_protocol_encoder_freezer_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    struct SubGhzProtocolEncoderFreezer* instance =
        malloc(sizeof(struct SubGhzProtocolEncoderFreezer));
    instance->base.protocol = &subghz_protocol_freezer;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = 3;
    instance->encoder.size_upload = 128;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_freezer_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    struct SubGhzProtocolEncoderFreezer* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) break;
        instance->encoder.size_upload = 128;
        subghz_protocol_encoder_freezer_get_upload(instance);
        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);
    return ret;
}

static const SubGhzProtocolEncoder enc_freezer_vt = {
    .alloc = subghz_protocol_encoder_freezer_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_freezer_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

const SubGhzProtocol subghz_protocol_freezer = {
    .name = SUBGHZ_PROTOCOL_FREEZER_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_freezer_vt,
};

static const SubGhzEncodePluginEntry enc_freezer_entry = {
    .name = SUBGHZ_PROTOCOL_FREEZER_NAME,
    .encoder = &enc_freezer_vt};
static const FlipperAppPluginDescriptor enc_freezer_descriptor = {
    .appid = SUBGHZ_ENCODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION,
    .entry_point = &enc_freezer_entry};
const FlipperAppPluginDescriptor* enc_freezer_ep(void) {
    return &enc_freezer_descriptor;
}
