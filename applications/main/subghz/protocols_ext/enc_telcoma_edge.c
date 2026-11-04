/* enc_telcoma_edge.fal : SD encoder for telcoma_edge (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_free enc_telcoma_edge__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_telcoma_edge__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_telcoma_edge__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_telcoma_edge_alloc enc_telcoma_edge__subghz_protocol_encoder_telcoma_edge_alloc
#define subghz_protocol_encoder_telcoma_edge_deserialize enc_telcoma_edge__subghz_protocol_encoder_telcoma_edge_deserialize
#define subghz_protocol_telcoma_edge enc_telcoma_edge__subghz_protocol_telcoma_edge
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/telcoma_edge.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolTelcomaEdge"
#define TELCOMA_EDGE_UPLOAD_SIZE 128
#define TELCOMA_EDGE_PUSH(lvl)                                                                    \
    do {                                                                                          \
        if(index > 0 && level_duration_get_level(instance->encoder.upload[index - 1]) == (lvl)) { \
            instance->encoder.upload[index - 1] = level_duration_make(                            \
                (lvl), level_duration_get_duration(instance->encoder.upload[index - 1]) + te);    \
        } else {                                                                                  \
            instance->encoder.upload[index++] = level_duration_make((lvl), te);                   \
        }                                                                                          \
    } while(0)

struct SubGhzProtocolEncoderTelcomaEdge {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_telcoma_edge_const = {
    .te_short = 1270,
    .te_long = 2540,
    .te_delta = 520,
    .min_count_bit_for_found = 32,
};

static bool
    subghz_protocol_encoder_telcoma_edge_get_upload(SubGhzProtocolEncoderTelcomaEdge* instance);

static bool
    subghz_protocol_encoder_telcoma_edge_get_upload(SubGhzProtocolEncoderTelcomaEdge* instance) {
    furi_assert(instance);
    size_t index = 0;
    uint32_t te = subghz_protocol_telcoma_edge_const.te_short;

    uint32_t key = (uint32_t)(instance->generic.data & 0xFFFFFFFF);
    uint8_t channel = key & 0x07;
    uint64_t value;
    uint8_t nbits;
    bool add_stop;
    if(channel == 0) {
        value = key;
        nbits = 32;
        add_stop = true;
    } else {
        value = (uint64_t)key << 1;
        nbits = 33;
        add_stop = false;
    }

    if((size_t)(nbits * 2 + 2) > TELCOMA_EDGE_UPLOAD_SIZE) {
        FURI_LOG_E(TAG, "Upload buffer too small");
        return false;
    }

#define TELCOMA_EDGE_PUSH(lvl)                                                                    \
    do {                                                                                          \
        if(index > 0 && level_duration_get_level(instance->encoder.upload[index - 1]) == (lvl)) { \
            instance->encoder.upload[index - 1] = level_duration_make(                            \
                (lvl), level_duration_get_duration(instance->encoder.upload[index - 1]) + te);    \
        } else {                                                                                  \
            instance->encoder.upload[index++] = level_duration_make((lvl), te);                   \
        }                                                                                          \
    } while(0)

    for(int i = nbits - 1; i >= 0; i--) {
        uint8_t bit = (value >> i) & 1;
        if(bit) {

            TELCOMA_EDGE_PUSH(true);
            TELCOMA_EDGE_PUSH(false);
        } else {

            TELCOMA_EDGE_PUSH(false);
            TELCOMA_EDGE_PUSH(true);
        }
    }
    if(add_stop) {

        TELCOMA_EDGE_PUSH(true);
    }
#undef TELCOMA_EDGE_PUSH

    instance->encoder.upload[index++] = level_duration_make(false, te * 4);

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

void* subghz_protocol_encoder_telcoma_edge_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderTelcomaEdge* instance = malloc(sizeof(SubGhzProtocolEncoderTelcomaEdge));

    instance->base.protocol = &subghz_protocol_telcoma_edge;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 6;
    instance->encoder.size_upload = TELCOMA_EDGE_UPLOAD_SIZE;
    instance->encoder.upload = malloc(TELCOMA_EDGE_UPLOAD_SIZE * sizeof(LevelDuration));
    instance->encoder.front = 0;
    instance->encoder.is_running = false;
    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_telcoma_edge_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderTelcomaEdge* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_telcoma_edge_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) break;

        if(!subghz_protocol_encoder_telcoma_edge_get_upload(instance)) {
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }
        instance->encoder.front = 0;
        instance->encoder.is_running = true;
    } while(false);
    return ret;
}

static const SubGhzProtocolEncoder enc_telcoma_edge_vt = {
    .alloc = subghz_protocol_encoder_telcoma_edge_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_telcoma_edge_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_telcoma_edge = {
    .name = SUBGHZ_PROTOCOL_TELCOMA_EDGE_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_telcoma_edge_vt,
};

static const SubGhzEncodePluginEntry enc_telcoma_edge_entry = {.name = SUBGHZ_PROTOCOL_TELCOMA_EDGE_NAME, .encoder = &enc_telcoma_edge_vt};
static const FlipperAppPluginDescriptor enc_telcoma_edge_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_telcoma_edge_entry};
const FlipperAppPluginDescriptor* enc_telcoma_edge_ep(void) { return &enc_telcoma_edge_descriptor; }
