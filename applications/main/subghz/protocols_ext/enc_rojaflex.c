

#define subghz_protocol_encoder_common_free enc_rojaflex__common_free
#define subghz_protocol_encoder_common_stop enc_rojaflex__common_stop
#define subghz_protocol_encoder_common_yield enc_rojaflex__common_yield
#define subghz_protocol_encoder_rojaflex_alloc enc_rojaflex__alloc
#define subghz_protocol_encoder_rojaflex_deserialize enc_rojaflex__deserialize
#define subghz_protocol_rojaflex enc_rojaflex__subghz_protocol_rojaflex

#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/rojaflex.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

#define ROJA_TE 100U
#define ROJA_PREAMBLE_BYTES 8
#define ROJA_MSG_MAX 11

struct SubGhzProtocolEncoderRojaflex {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    uint8_t msg[ROJA_MSG_MAX];
    uint16_t frame_bits;
};

static void subghz_protocol_encoder_rojaflex_get_upload(struct SubGhzProtocolEncoderRojaflex* instance) {
    furi_assert(instance);
    int nbytes = instance->frame_bits / 8;
    if(nbytes < 1 || nbytes > ROJA_MSG_MAX) nbytes = 9;

    uint8_t buf[ROJA_PREAMBLE_BYTES + 4 + ROJA_MSG_MAX];
    int n = 0;
    for(int i = 0; i < ROJA_PREAMBLE_BYTES; i++) buf[n++] = 0xAA;
    buf[n++] = 0xD3;
    buf[n++] = 0x91;
    buf[n++] = 0xD3;
    buf[n++] = 0x91;
    for(int i = 0; i < nbytes; i++) buf[n++] = instance->msg[i];

    int total_bits = n * 8;
    size_t index = 0;
    int prev = -1;
    uint32_t run = 0;
    for(int i = 0; i < total_bits; i++) {
        int bit = (buf[i >> 3] >> (7 - (i & 7))) & 1;
        if(bit == prev) {
            run++;
        } else {
            if(prev >= 0 && index < instance->encoder.size_upload)
                instance->encoder.upload[index++] = level_duration_make(prev != 0, run * ROJA_TE);
            prev = bit;
            run = 1;
        }
    }
    if(prev >= 0 && index < instance->encoder.size_upload)
        instance->encoder.upload[index++] = level_duration_make(prev != 0, run * ROJA_TE);

    if(index < instance->encoder.size_upload)
        instance->encoder.upload[index++] = level_duration_make(false, 5000);

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

void* subghz_protocol_encoder_rojaflex_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    struct SubGhzProtocolEncoderRojaflex* instance =
        malloc(sizeof(struct SubGhzProtocolEncoderRojaflex));
    instance->base.protocol = &subghz_protocol_rojaflex;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = 4;
    instance->encoder.size_upload = 256;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    memset(instance->msg, 0, sizeof(instance->msg));
    instance->frame_bits = 88;
    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_rojaflex_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    struct SubGhzProtocolEncoderRojaflex* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) break;
        uint32_t fb = instance->generic.data_count_bit;
        flipper_format_rewind(flipper_format);
        flipper_format_read_uint32(flipper_format, "FrameBits", &fb, 1);
        flipper_format_rewind(flipper_format);
        if(!flipper_format_read_hex(flipper_format, "Frame", instance->msg, ROJA_MSG_MAX)) {
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        instance->frame_bits = (uint16_t)fb;
        instance->encoder.size_upload = 256;
        subghz_protocol_encoder_rojaflex_get_upload(instance);
        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);
    return ret;
}

static const SubGhzProtocolEncoder enc_rojaflex_vt = {
    .alloc = subghz_protocol_encoder_rojaflex_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_rojaflex_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

const SubGhzProtocol subghz_protocol_rojaflex = {
    .name = SUBGHZ_PROTOCOL_ROJAFLEX_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_rojaflex_vt,
};

static const SubGhzEncodePluginEntry enc_rojaflex_entry = {
    .name = SUBGHZ_PROTOCOL_ROJAFLEX_NAME,
    .encoder = &enc_rojaflex_vt};
static const FlipperAppPluginDescriptor enc_rojaflex_descriptor = {
    .appid = SUBGHZ_ENCODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION,
    .entry_point = &enc_rojaflex_entry};
const FlipperAppPluginDescriptor* enc_rojaflex_ep(void) {
    return &enc_rojaflex_descriptor;
}
