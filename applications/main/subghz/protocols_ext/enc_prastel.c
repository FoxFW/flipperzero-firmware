

#define subghz_protocol_encoder_common_free enc_prastel__common_free
#define subghz_protocol_encoder_common_stop enc_prastel__common_stop
#define subghz_protocol_encoder_common_yield enc_prastel__common_yield
#define subghz_protocol_encoder_prastel_alloc enc_prastel__alloc
#define subghz_protocol_encoder_prastel_deserialize enc_prastel__deserialize
#define subghz_protocol_prastel enc_prastel__subghz_protocol_prastel

#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/prastel.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

#define PRA_TE_SHORT 320U
#define PRA_TE_LONG  640U
#define PRA_FRAME_BITS 42U
#define PRA_HEADER_US (PRA_TE_SHORT * 30U)

struct SubGhzProtocolEncoderPrastel {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static void subghz_protocol_encoder_prastel_get_upload(struct SubGhzProtocolEncoderPrastel* instance) {
    furi_assert(instance);
    size_t index = 0;
    uint64_t data = instance->generic.data;

    instance->encoder.upload[index++] = level_duration_make(false, PRA_HEADER_US);
    instance->encoder.upload[index++] = level_duration_make(true, PRA_TE_SHORT);
    for(int i = (int)PRA_FRAME_BITS - 1; i >= 0; i--) {
        bool bit = (data >> i) & 1U;
        instance->encoder.upload[index++] =
            level_duration_make(false, bit ? PRA_TE_LONG : PRA_TE_SHORT);
        instance->encoder.upload[index++] =
            level_duration_make(true, bit ? PRA_TE_SHORT : PRA_TE_LONG);
    }
    instance->encoder.upload[index++] = level_duration_make(false, PRA_HEADER_US);

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

void* subghz_protocol_encoder_prastel_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    struct SubGhzProtocolEncoderPrastel* instance =
        malloc(sizeof(struct SubGhzProtocolEncoderPrastel));
    instance->base.protocol = &subghz_protocol_prastel;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = 6;
    instance->encoder.size_upload = 128;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_prastel_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    struct SubGhzProtocolEncoderPrastel* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) break;
        if(instance->generic.data_count_bit != PRA_FRAME_BITS) {
            ret = SubGhzProtocolStatusErrorValueBitCount;
            break;
        }
        instance->encoder.size_upload = 128;
        subghz_protocol_encoder_prastel_get_upload(instance);
        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);
    return ret;
}

static const SubGhzProtocolEncoder enc_prastel_vt = {
    .alloc = subghz_protocol_encoder_prastel_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_prastel_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

const SubGhzProtocol subghz_protocol_prastel = {
    .name = SUBGHZ_PROTOCOL_PRASTEL_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_prastel_vt,
};

static const SubGhzEncodePluginEntry enc_prastel_entry = {
    .name = SUBGHZ_PROTOCOL_PRASTEL_NAME,
    .encoder = &enc_prastel_vt};
static const FlipperAppPluginDescriptor enc_prastel_descriptor = {
    .appid = SUBGHZ_ENCODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION,
    .entry_point = &enc_prastel_entry};
const FlipperAppPluginDescriptor* enc_prastel_ep(void) {
    return &enc_prastel_descriptor;
}
