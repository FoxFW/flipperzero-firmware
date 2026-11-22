

#define subghz_protocol_proteco enc_proteco__subghz_protocol_proteco
#define subghz_protocol_encoder_proteco_alloc enc_proteco__subghz_protocol_encoder_proteco_alloc
#define subghz_protocol_encoder_proteco_deserialize \
    enc_proteco__subghz_protocol_encoder_proteco_deserialize
#define subghz_protocol_encoder_common_alloc enc_proteco__subghz_protocol_encoder_common_alloc
#define subghz_protocol_encoder_common_free enc_proteco__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_proteco__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_proteco__subghz_protocol_encoder_common_yield
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/proteco.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolProteco"

#define PROTECO_GUARD_TE_X2 41u
#define PROTECO_BTN_MASK    0x3u
static const uint8_t subghz_protocol_proteco_btn_raw[5] = {0x0, 0x2, 0x0, 0x1, 0x3};

static const SubGhzBlockConst subghz_protocol_proteco_const = {
    .te_short = 440,
    .te_long = 880,
    .te_delta = 150,
    .min_count_bit_for_found = 24};

struct SubGhzProtocolEncoderProteco {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static uint8_t subghz_protocol_proteco_get_btn_code(void) {
    uint8_t original_btn_code = subghz_custom_btn_get_original();
    uint8_t custom_btn_id = subghz_custom_btn_get();
    if((original_btn_code == 0) || (custom_btn_id == SUBGHZ_CUSTOM_BTN_OK)) {
        return original_btn_code;
    }
    return (uint8_t)(((original_btn_code - 1u + custom_btn_id) & PROTECO_BTN_MASK) + 1u);
}

static bool subghz_protocol_encoder_proteco_get_upload(void* context) {
    SubGhzProtocolEncoderProteco* instance = context;
    const SubGhzBlockConst* c = &subghz_protocol_proteco_const;
    size_t index = 0;
    uint8_t btn = subghz_protocol_proteco_get_btn_code();
    if(subghz_block_generic_global_button_override_get(&btn)) {
        FURI_LOG_D(TAG, "Button sucessfully changed to 0x%X", btn);
    }
    if((btn >= 1) && (btn <= 4)) {
        instance->generic.data = (instance->generic.data & ~(uint64_t)PROTECO_BTN_MASK) |
                                 subghz_protocol_proteco_btn_raw[btn];
    }
    instance->encoder.upload[index++] =
        level_duration_make(false, c->te_short * PROTECO_GUARD_TE_X2 / 2);
    instance->encoder.upload[index++] = level_duration_make(true, c->te_short);
    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if(bit_read(instance->generic.data, i - 1)) {
            instance->encoder.upload[index++] = level_duration_make(false, c->te_long);
            instance->encoder.upload[index++] = level_duration_make(true, c->te_short);
        } else {
            instance->encoder.upload[index++] = level_duration_make(false, c->te_short);
            instance->encoder.upload[index++] = level_duration_make(true, c->te_long);
        }
    }
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

void* subghz_protocol_encoder_proteco_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderProteco), &subghz_protocol_proteco, 10, 50);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_proteco_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderProteco* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_proteco_const.min_count_bit_for_found);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }
    flipper_format_read_uint32(flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);
    if(!subghz_protocol_encoder_proteco_get_upload(instance)) {
        return SubGhzProtocolStatusErrorEncoderGetUpload;
    }
    instance->encoder.is_running = true;
    return ret;
}

static const SubGhzProtocolEncoder enc_proteco_vt = {
    .alloc = subghz_protocol_encoder_proteco_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_proteco_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

const SubGhzProtocol subghz_protocol_proteco = {
    .name = SUBGHZ_PROTOCOL_PROTECO_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_proteco_vt,
};

static const SubGhzEncodePluginEntry enc_proteco_entry = {
    .name = SUBGHZ_PROTOCOL_PROTECO_NAME,
    .encoder = &enc_proteco_vt};
static const FlipperAppPluginDescriptor enc_proteco_descriptor = {
    .appid = SUBGHZ_ENCODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION,
    .entry_point = &enc_proteco_entry};
const FlipperAppPluginDescriptor* enc_proteco_ep(void) {
    return &enc_proteco_descriptor;
}
