

#define subghz_protocol_encoder_common_free enc_sti3353__common_free
#define subghz_protocol_encoder_common_stop enc_sti3353__common_stop
#define subghz_protocol_encoder_common_yield enc_sti3353__common_yield
#define subghz_protocol_encoder_sti3353_alloc enc_sti3353__alloc
#define subghz_protocol_encoder_sti3353_deserialize enc_sti3353__deserialize
#define subghz_protocol_sti3353 enc_sti3353__subghz_protocol_sti3353

#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/sti3353.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

#define STI_TE_SHORT 200U
#define STI_TE_LONG  600U
#define STI_FRAME_BITS 48U
#define STI_GUARD_US 6000U

struct SubGhzProtocolEncoderSTI3353 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static void subghz_protocol_encoder_sti3353_get_upload(struct SubGhzProtocolEncoderSTI3353* instance) {
    furi_assert(instance);
    size_t index = 0;
    uint64_t data = instance->generic.data;

    instance->encoder.upload[index++] = level_duration_make(false, STI_GUARD_US);
    for(int frame = 0; frame < 2; frame++) {
        for(int i = (int)STI_FRAME_BITS - 1; i >= 0; i--) {
            bool bit = (data >> i) & 1U;
            instance->encoder.upload[index++] =
                level_duration_make(true, bit ? STI_TE_LONG : STI_TE_SHORT);
            instance->encoder.upload[index++] =
                level_duration_make(false, bit ? STI_TE_SHORT : STI_TE_LONG);
        }
        instance->encoder.upload[index++] = level_duration_make(false, STI_GUARD_US);
    }
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

void* subghz_protocol_encoder_sti3353_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    struct SubGhzProtocolEncoderSTI3353* instance =
        malloc(sizeof(struct SubGhzProtocolEncoderSTI3353));
    instance->base.protocol = &subghz_protocol_sti3353;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = 3;
    instance->encoder.size_upload = 256;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_sti3353_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    struct SubGhzProtocolEncoderSTI3353* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) break;
        if(instance->generic.data_count_bit != STI_FRAME_BITS) {
            ret = SubGhzProtocolStatusErrorValueBitCount;
            break;
        }
        instance->encoder.size_upload = 256;
        subghz_protocol_encoder_sti3353_get_upload(instance);
        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);
    return ret;
}

static const SubGhzProtocolEncoder enc_sti3353_vt = {
    .alloc = subghz_protocol_encoder_sti3353_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_sti3353_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

const SubGhzProtocol subghz_protocol_sti3353 = {
    .name = SUBGHZ_PROTOCOL_STI3353_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_sti3353_vt,
};

static const SubGhzEncodePluginEntry enc_sti3353_entry = {
    .name = SUBGHZ_PROTOCOL_STI3353_NAME,
    .encoder = &enc_sti3353_vt};
static const FlipperAppPluginDescriptor enc_sti3353_descriptor = {
    .appid = SUBGHZ_ENCODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION,
    .entry_point = &enc_sti3353_entry};
const FlipperAppPluginDescriptor* enc_sti3353_ep(void) {
    return &enc_sti3353_descriptor;
}
