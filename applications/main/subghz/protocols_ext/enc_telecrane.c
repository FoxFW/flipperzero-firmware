

#define subghz_protocol_encoder_common_alloc enc_telecrane__subghz_protocol_encoder_common_alloc
#define subghz_protocol_encoder_common_free enc_telecrane__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_telecrane__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_telecrane__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_telecrane_alloc enc_telecrane__subghz_protocol_encoder_telecrane_alloc
#define subghz_protocol_encoder_telecrane_deserialize \
    enc_telecrane__subghz_protocol_encoder_telecrane_deserialize
#define subghz_protocol_telecrane enc_telecrane__subghz_protocol_telecrane
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/telecrane.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolTelecrane"

#define TELECRANE_BIT_US        53u
#define TELECRANE_MIN_COUNT_BIT 24
#define TELECRANE_TPL_NIBBLES   50
#define TELECRANE_TPL_BITS      200
#define TELECRANE_STRIP         3
#define TELECRANE_FRAME_BITS    (TELECRANE_TPL_BITS - TELECRANE_STRIP)
#define TELECRANE_PERIOD_CH8_US 19940u
#define TELECRANE_PERIOD_CH9_US 30023u
#define TELECRANE_UPLOAD_MAX    (TELECRANE_FRAME_BITS + 1)

static const uint8_t telecrane_tpl_ch8[TELECRANE_TPL_NIBBLES] = {
    0x1, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5,
    0x5, 0x5, 0x5, 0x5, 0x4, 0x5, 0xb, 0xa, 0x8, 0x5, 0xb, 0xa, 0x8, 0x0, 0x0, 0x1,
    0x0, 0x1, 0x6, 0x4, 0x4, 0x0, 0x6, 0x0, 0x0, 0x0, 0x0, 0x7, 0x0, 0xa, 0x8, 0x0,
    0x0, 0x0};
static const uint8_t telecrane_tpl_ch9[TELECRANE_TPL_NIBBLES] = {
    0x1, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5,
    0x5, 0x5, 0x5, 0x5, 0x4, 0x5, 0xb, 0xa, 0x8, 0x5, 0xb, 0xa, 0x8, 0x0, 0x0, 0x1,
    0x2, 0x0, 0xa, 0x2, 0xe, 0x1, 0xa, 0x0, 0x0, 0x0, 0x0, 0x6, 0x4, 0x0, 0x0, 0x0,
    0x0, 0x1};

struct SubGhzProtocolEncoderTelecrane {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint16_t cmd_mask;
    uint8_t channel;
};

static bool subghz_protocol_encoder_telecrane_get_upload(SubGhzProtocolEncoderTelecrane* instance) {
    furi_assert(instance);

    if(TELECRANE_UPLOAD_MAX > instance->encoder.size_upload) {
        FURI_LOG_E(TAG, "Size upload exceeds allocated encoder buffer.");
        return false;
    }

    const uint8_t ch = instance->channel;
    const bool near9 = (ch >= 9);
    const uint8_t* tpl = near9 ? telecrane_tpl_ch9 : telecrane_tpl_ch8;
    const uint16_t base = near9 ? 0x0006 : 0x0007;
    const uint32_t period = near9 ? TELECRANE_PERIOD_CH9_US : TELECRANE_PERIOD_CH8_US;
    const uint16_t mask = instance->cmd_mask;
    const uint16_t field = (uint16_t)(base + mask);
    const uint32_t id = (uint32_t)(instance->generic.data & 0xFFFFFF);

    uint8_t bits[TELECRANE_TPL_BITS];
    for(size_t i = 0; i < TELECRANE_TPL_NIBBLES; i++) {
        for(int k = 3; k >= 0; k--) {
            bits[4 * i + (3 - k)] = (uint8_t)((tpl[i] >> k) & 1);
        }
    }
    for(int k = 0; k < 24; k++) bits[131 + k] = (uint8_t)((id >> (23 - k)) & 1);
    for(int k = 0; k < 5; k++) bits[155 + k] = (uint8_t)((mask >> (4 - k)) & 1);
    for(int k = 0; k < 16; k++) bits[160 + k] = (uint8_t)((field >> (15 - k)) & 1);

    size_t index = 0;
    size_t i = TELECRANE_STRIP;
    while(i < TELECRANE_TPL_BITS) {
        uint8_t lvl = bits[i];
        size_t j = i;
        while(j < TELECRANE_TPL_BITS && bits[j] == lvl) j++;
        uint32_t run = (uint32_t)(j - i) * TELECRANE_BIT_US;
        instance->encoder.upload[index++] = level_duration_make(lvl != 0, run);
        i = j;
    }

    const uint32_t frame_us = (uint32_t)TELECRANE_FRAME_BITS * TELECRANE_BIT_US;
    const uint32_t gap = (period > frame_us) ? (period - frame_us) : TELECRANE_BIT_US;
    instance->encoder.upload[index++] = level_duration_make(false, gap);

    instance->encoder.size_upload = index;
    return true;
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

void* subghz_protocol_encoder_common_alloc(
    size_t instance_size,
    const SubGhzProtocol* protocol,
    size_t repeat,
    size_t size_upload) {
    SubGhzProtocolEncoderCommonGeneric* instance = malloc(instance_size);
    instance->common.base.protocol = protocol;
    instance->generic.protocol_name = protocol->name;
    instance->common.encoder.repeat = repeat;
    instance->common.encoder.size_upload = size_upload;
    instance->common.encoder.upload = malloc(size_upload * sizeof(LevelDuration));
    instance->common.encoder.is_running = false;
    return instance;
}

void* subghz_protocol_encoder_telecrane_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderTelecrane), &subghz_protocol_telecrane, 10, TELECRANE_UPLOAD_MAX);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_telecrane_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderTelecrane* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, TELECRANE_MIN_COUNT_BIT);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }
    uint32_t ch = 8, cmd = 0;
    flipper_format_read_uint32(flipper_format, "Channel", &ch, 1);
    flipper_format_read_uint32(flipper_format, "Cmd", &cmd, 1);
    instance->channel = (uint8_t)ch;
    instance->cmd_mask = (uint16_t)cmd;

    flipper_format_read_uint32(flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

    if(!subghz_protocol_encoder_telecrane_get_upload(instance)) {
        return SubGhzProtocolStatusErrorEncoderGetUpload;
    }
    instance->encoder.is_running = true;
    return ret;
}

static const SubGhzProtocolEncoder enc_telecrane_vt = {
    .alloc = subghz_protocol_encoder_telecrane_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_telecrane_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

const SubGhzProtocol subghz_protocol_telecrane = {
    .name = SUBGHZ_PROTOCOL_TELECRANE_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_868 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_telecrane_vt,
};

static const SubGhzEncodePluginEntry enc_telecrane_entry = {
    .name = SUBGHZ_PROTOCOL_TELECRANE_NAME,
    .encoder = &enc_telecrane_vt};
static const FlipperAppPluginDescriptor enc_telecrane_descriptor = {
    .appid = SUBGHZ_ENCODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION,
    .entry_point = &enc_telecrane_entry};
const FlipperAppPluginDescriptor* enc_telecrane_ep(void) {
    return &enc_telecrane_descriptor;
}
