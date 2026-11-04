/* enc_linear_delta3.fal : SD encoder for linear_delta3 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_alloc enc_linear_delta3__subghz_protocol_encoder_common_alloc
#define subghz_protocol_encoder_common_deserialize enc_linear_delta3__subghz_protocol_encoder_common_deserialize
#define subghz_protocol_encoder_common_free enc_linear_delta3__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_linear_delta3__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_linear_delta3__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_linear_delta3_alloc enc_linear_delta3__subghz_protocol_encoder_linear_delta3_alloc
#define subghz_protocol_encoder_linear_delta3_deserialize enc_linear_delta3__subghz_protocol_encoder_linear_delta3_deserialize
#define subghz_protocol_linear_delta3 enc_linear_delta3__subghz_protocol_linear_delta3
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/linear_delta3.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolLinearDelta3"

struct SubGhzProtocolEncoderLinearDelta3 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_linear_delta3_const = {
    .te_short = 500,
    .te_long = 2000,
    .te_delta = 150,
    .min_count_bit_for_found = 8,
};

static bool
    subghz_protocol_encoder_linear_delta3_get_upload(SubGhzProtocolEncoderLinearDelta3* instance);

static bool
    subghz_protocol_encoder_linear_delta3_get_upload(SubGhzProtocolEncoderLinearDelta3* instance) {
    furi_assert(instance);
    size_t index = 0;
    size_t size_upload = (instance->generic.data_count_bit * 2);
    if(size_upload > instance->encoder.size_upload) {
        FURI_LOG_E(TAG, "Size upload exceeds allocated encoder buffer.");
        return false;
    } else {
        instance->encoder.size_upload = size_upload;
    }

    for(uint8_t i = instance->generic.data_count_bit; i > 1; i--) {
        if(bit_read(instance->generic.data, i - 1)) {

            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_linear_delta3_const.te_short);
            instance->encoder.upload[index++] = level_duration_make(
                false, (uint32_t)subghz_protocol_linear_delta3_const.te_short * 7);
        } else {

            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_linear_delta3_const.te_long);
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_linear_delta3_const.te_long);
        }
    }

    if(bit_read(instance->generic.data, 0)) {

        instance->encoder.upload[index++] =
            level_duration_make(true, (uint32_t)subghz_protocol_linear_delta3_const.te_short);

        instance->encoder.upload[index] = level_duration_make(
            false, (uint32_t)subghz_protocol_linear_delta3_const.te_short * 73);
    } else {

        instance->encoder.upload[index++] =
            level_duration_make(true, (uint32_t)subghz_protocol_linear_delta3_const.te_long);

        instance->encoder.upload[index] = level_duration_make(
            false, (uint32_t)subghz_protocol_linear_delta3_const.te_short * 70);
    }

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

/* vendored from lib/subghz/protocols/common.c (not exported) */
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

/* vendored from lib/subghz/protocols/common.c (not exported) */
SubGhzProtocolStatus subghz_protocol_encoder_common_deserialize(
    void* context,
    FlipperFormat* flipper_format,
    uint16_t min_count_bit,
    SubGhzProtocolEncoderGetUpload get_upload) {
    furi_assert(context);
    SubGhzProtocolEncoderCommonGeneric* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, min_count_bit);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }
    flipper_format_read_uint32(
        flipper_format, "Repeat", (uint32_t*)&instance->common.encoder.repeat, 1);

    if(!get_upload(instance)) {
        return SubGhzProtocolStatusErrorEncoderGetUpload;
    }
    instance->common.encoder.is_running = true;
    return ret;
}

void* subghz_protocol_encoder_linear_delta3_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderLinearDelta3), &subghz_protocol_linear_delta3, 3, 16);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_linear_delta3_deserialize(void* context, FlipperFormat* flipper_format) {
    return subghz_protocol_encoder_common_deserialize(
        context, flipper_format, subghz_protocol_linear_delta3_const.min_count_bit_for_found, (SubGhzProtocolEncoderGetUpload)subghz_protocol_encoder_linear_delta3_get_upload);
}

static const SubGhzProtocolEncoder enc_linear_delta3_vt = {
    .alloc = subghz_protocol_encoder_linear_delta3_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_linear_delta3_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_linear_delta3 = {
    .name = SUBGHZ_PROTOCOL_LINEAR_DELTA3_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_linear_delta3_vt,
};

static const SubGhzEncodePluginEntry enc_linear_delta3_entry = {.name = SUBGHZ_PROTOCOL_LINEAR_DELTA3_NAME, .encoder = &enc_linear_delta3_vt};
static const FlipperAppPluginDescriptor enc_linear_delta3_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_linear_delta3_entry};
const FlipperAppPluginDescriptor* enc_linear_delta3_ep(void) { return &enc_linear_delta3_descriptor; }
