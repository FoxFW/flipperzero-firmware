

#define subghz_protocol_encoder_common_free enc_faac_tm__common_free
#define subghz_protocol_encoder_common_stop enc_faac_tm__common_stop
#define subghz_protocol_encoder_common_yield enc_faac_tm__common_yield
#define subghz_protocol_encoder_faac_tm_alloc enc_faac_tm__alloc
#define subghz_protocol_encoder_faac_tm_deserialize enc_faac_tm__deserialize
#define subghz_protocol_faac_tm enc_faac_tm__subghz_protocol_faac_tm

#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/faac_tm.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

#define FTM_TE_NOMINAL 400U
#define FTM_HEADER_TE  35U
#define FTM_TE_MIN     100U
#define FTM_TE_MAX     1000U
#define FTM_FRAME_BITS 12U

struct SubGhzProtocolEncoderFaacTm {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    uint32_t te;
};

static void subghz_protocol_encoder_faac_tm_get_upload(struct SubGhzProtocolEncoderFaacTm* instance) {
    furi_assert(instance);
    size_t index = 0;
    uint32_t te = instance->te;
    uint64_t data = instance->generic.data;
    instance->encoder.upload[index++] = level_duration_make(false, te * FTM_HEADER_TE);
    instance->encoder.upload[index++] = level_duration_make(true, te);
    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if((data >> (i - 1)) & 1U) {
            instance->encoder.upload[index++] = level_duration_make(false, te);
            instance->encoder.upload[index++] = level_duration_make(true, te * 2);
        } else {
            instance->encoder.upload[index++] = level_duration_make(false, te * 2);
            instance->encoder.upload[index++] = level_duration_make(true, te);
        }
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

void* subghz_protocol_encoder_faac_tm_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    struct SubGhzProtocolEncoderFaacTm* instance = malloc(sizeof(struct SubGhzProtocolEncoderFaacTm));
    instance->base.protocol = &subghz_protocol_faac_tm;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = 3;
    instance->encoder.size_upload = 128;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    instance->te = FTM_TE_NOMINAL;
    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_faac_tm_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    struct SubGhzProtocolEncoderFaacTm* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic, flipper_format, FTM_FRAME_BITS);
        if(ret != SubGhzProtocolStatusOk) break;
        uint32_t te = 0;
        instance->te = FTM_TE_NOMINAL;
        if(flipper_format_rewind(flipper_format) &&
           flipper_format_read_uint32(flipper_format, "TE", &te, 1) && (te > FTM_TE_MIN) &&
           (te < FTM_TE_MAX)) {
            instance->te = te;
        }
        instance->encoder.size_upload = 128;
        subghz_protocol_encoder_faac_tm_get_upload(instance);
        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);
    return ret;
}

static const SubGhzProtocolEncoder enc_faac_tm_vt = {
    .alloc = subghz_protocol_encoder_faac_tm_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_faac_tm_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

const SubGhzProtocol subghz_protocol_faac_tm = {
    .name = SUBGHZ_PROTOCOL_FAAC_TM_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_faac_tm_vt,
};

static const SubGhzEncodePluginEntry enc_faac_tm_entry = {
    .name = SUBGHZ_PROTOCOL_FAAC_TM_NAME,
    .encoder = &enc_faac_tm_vt};
static const FlipperAppPluginDescriptor enc_faac_tm_descriptor = {
    .appid = SUBGHZ_ENCODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION,
    .entry_point = &enc_faac_tm_entry};
const FlipperAppPluginDescriptor* enc_faac_tm_ep(void) {
    return &enc_faac_tm_descriptor;
}
