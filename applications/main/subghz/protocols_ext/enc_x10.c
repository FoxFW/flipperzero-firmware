

#define subghz_protocol_encoder_common_free enc_x10__common_free
#define subghz_protocol_encoder_common_stop enc_x10__common_stop
#define subghz_protocol_encoder_common_yield enc_x10__common_yield
#define subghz_protocol_encoder_x10_alloc enc_x10__alloc
#define subghz_protocol_encoder_x10_deserialize enc_x10__deserialize
#define subghz_protocol_x10 enc_x10__subghz_protocol_x10

#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/x10.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

#define X10_TE_SHORT 600U
#define X10_TE_LONG  1800U
#define X10_FRAME_BITS 32U

struct SubGhzProtocolEncoderX10 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static void subghz_protocol_encoder_x10_get_upload(struct SubGhzProtocolEncoderX10* instance) {
    furi_assert(instance);
    size_t index = 0;
    const uint32_t te = X10_TE_SHORT;

    instance->encoder.upload[index++] = level_duration_make(true, te * 16);
    instance->encoder.upload[index++] = level_duration_make(false, te * 8);

    for(int i = (int)X10_FRAME_BITS - 1; i >= 0; i--) {
        bool bit = (instance->generic.data >> i) & 1U;
        instance->encoder.upload[index++] = level_duration_make(true, te);
        instance->encoder.upload[index++] = level_duration_make(false, bit ? X10_TE_LONG : te);
    }

    instance->encoder.upload[index++] = level_duration_make(true, te);
    instance->encoder.upload[index++] = level_duration_make(false, te * 20);

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

void* subghz_protocol_encoder_x10_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    struct SubGhzProtocolEncoderX10* instance = malloc(sizeof(struct SubGhzProtocolEncoderX10));
    instance->base.protocol = &subghz_protocol_x10;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = 6;
    instance->encoder.size_upload = 128;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_x10_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    struct SubGhzProtocolEncoderX10* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) break;
        if(instance->generic.data_count_bit != X10_FRAME_BITS) {
            ret = SubGhzProtocolStatusErrorValueBitCount;
            break;
        }
        instance->encoder.size_upload = 128;
        subghz_protocol_encoder_x10_get_upload(instance);
        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);
    return ret;
}

static const SubGhzProtocolEncoder enc_x10_vt = {
    .alloc = subghz_protocol_encoder_x10_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_x10_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

const SubGhzProtocol subghz_protocol_x10 = {
    .name = SUBGHZ_PROTOCOL_X10_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_x10_vt,
};

static const SubGhzEncodePluginEntry enc_x10_entry = {
    .name = SUBGHZ_PROTOCOL_X10_NAME,
    .encoder = &enc_x10_vt};
static const FlipperAppPluginDescriptor enc_x10_descriptor = {
    .appid = SUBGHZ_ENCODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION,
    .entry_point = &enc_x10_entry};
const FlipperAppPluginDescriptor* enc_x10_ep(void) {
    return &enc_x10_descriptor;
}
