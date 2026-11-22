

#define subghz_protocol_encoder_common_free \
    enc_subaru_liberty_gt__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop \
    enc_subaru_liberty_gt__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield \
    enc_subaru_liberty_gt__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_subaru_liberty_gt_alloc \
    enc_subaru_liberty_gt__alloc
#define subghz_protocol_encoder_subaru_liberty_gt_deserialize \
    enc_subaru_liberty_gt__deserialize
#define subghz_protocol_subaru_liberty_gt enc_subaru_liberty_gt__subghz_protocol_subaru_liberty_gt

#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/subaru_liberty_gt.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

#define SLGT_TE_SHORT  700U
#define SLGT_FRAME_BITS 128U

struct SubGhzProtocolEncoderSubaruLibertyGt {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint64_t data_hi;
    uint64_t data_lo;
};

static void subghz_protocol_encoder_subaru_liberty_gt_get_upload(
    struct SubGhzProtocolEncoderSubaruLibertyGt* instance) {
    furi_assert(instance);
    const uint32_t te = SLGT_TE_SHORT;
    size_t index = 0;

    int pend_level = -1;
    uint32_t pend_halves = 0;

    for(int i = 0; i < (int)SLGT_FRAME_BITS; i++) {
        uint8_t bit = (i < 64) ? (uint8_t)((instance->data_hi >> (63 - i)) & 1U) :
                                 (uint8_t)((instance->data_lo >> (63 - (i - 64))) & 1U);

        uint8_t h0 = bit ? 1u : 0u;
        uint8_t h1 = bit ? 0u : 1u;
        uint8_t halves[2] = {h0, h1};
        for(int k = 0; k < 2; k++) {
            if((int)halves[k] == pend_level) {
                pend_halves++;
            } else {
                if(pend_level >= 0 && index < instance->encoder.size_upload) {
                    instance->encoder.upload[index++] =
                        level_duration_make(pend_level != 0, pend_halves * te);
                }
                pend_level = halves[k];
                pend_halves = 1;
            }
        }
    }
    if(pend_level >= 0 && index < instance->encoder.size_upload) {
        instance->encoder.upload[index++] = level_duration_make(pend_level != 0, pend_halves * te);
    }

    if(index < instance->encoder.size_upload)
        instance->encoder.upload[index++] = level_duration_make(false, 10000);

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

void* subghz_protocol_encoder_subaru_liberty_gt_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    struct SubGhzProtocolEncoderSubaruLibertyGt* instance =
        malloc(sizeof(struct SubGhzProtocolEncoderSubaruLibertyGt));
    instance->base.protocol = &subghz_protocol_subaru_liberty_gt;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = 10;
    instance->encoder.size_upload = 512;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    instance->data_hi = 0;
    instance->data_lo = 0;
    return instance;
}

SubGhzProtocolStatus subghz_protocol_encoder_subaru_liberty_gt_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_assert(context);
    struct SubGhzProtocolEncoderSubaruLibertyGt* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) break;

        uint8_t frame[16] = {0};
        flipper_format_rewind(flipper_format);
        if(!flipper_format_read_hex(flipper_format, "Frame", frame, 16)) {

            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        instance->data_hi = 0;
        instance->data_lo = 0;
        for(int i = 0; i < 8; i++) instance->data_hi = (instance->data_hi << 8) | frame[i];
        for(int i = 0; i < 8; i++) instance->data_lo = (instance->data_lo << 8) | frame[8 + i];

        instance->encoder.size_upload = 512;
        subghz_protocol_encoder_subaru_liberty_gt_get_upload(instance);
        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);
    return ret;
}

static const SubGhzProtocolEncoder enc_subaru_liberty_gt_vt = {
    .alloc = subghz_protocol_encoder_subaru_liberty_gt_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_subaru_liberty_gt_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

const SubGhzProtocol subghz_protocol_subaru_liberty_gt = {
    .name = SUBGHZ_PROTOCOL_SUBARU_LIBERTY_GT_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_subaru_liberty_gt_vt,
};

static const SubGhzEncodePluginEntry enc_subaru_liberty_gt_entry = {
    .name = SUBGHZ_PROTOCOL_SUBARU_LIBERTY_GT_NAME,
    .encoder = &enc_subaru_liberty_gt_vt};
static const FlipperAppPluginDescriptor enc_subaru_liberty_gt_descriptor = {
    .appid = SUBGHZ_ENCODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION,
    .entry_point = &enc_subaru_liberty_gt_entry};
const FlipperAppPluginDescriptor* enc_subaru_liberty_gt_ep(void) {
    return &enc_subaru_liberty_gt_descriptor;
}
