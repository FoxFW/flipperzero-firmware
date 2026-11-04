/* enc_dooya.fal : SD encoder for dooya (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_dooya enc_dooya__subghz_protocol_dooya
#define subghz_protocol_encoder_common_alloc enc_dooya__subghz_protocol_encoder_common_alloc
#define subghz_protocol_encoder_common_deserialize enc_dooya__subghz_protocol_encoder_common_deserialize
#define subghz_protocol_encoder_common_free enc_dooya__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_dooya__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_dooya__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_dooya_alloc enc_dooya__subghz_protocol_encoder_dooya_alloc
#define subghz_protocol_encoder_dooya_deserialize enc_dooya__subghz_protocol_encoder_dooya_deserialize
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/dooya.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolDooya"

struct SubGhzProtocolEncoderDooya {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_dooya_const = {
    .te_short = 366,
    .te_long = 733,
    .te_delta = 120,
    .min_count_bit_for_found = 40,
};

static bool subghz_protocol_encoder_dooya_get_upload(SubGhzProtocolEncoderDooya* instance);

static bool subghz_protocol_encoder_dooya_get_upload(SubGhzProtocolEncoderDooya* instance) {
    furi_assert(instance);

    size_t index = 0;
    size_t size_upload = (instance->generic.data_count_bit * 2) + 2;
    if(size_upload > instance->encoder.size_upload) {
        FURI_LOG_E(TAG, "Size upload exceeds allocated encoder buffer.");
        return false;
    } else {
        instance->encoder.size_upload = size_upload;
    }

    if(bit_read(instance->generic.data, 0)) {
        instance->encoder.upload[index++] = level_duration_make(
            false,
            (uint32_t)subghz_protocol_dooya_const.te_long * 12 +
                subghz_protocol_dooya_const.te_long);
    } else {
        instance->encoder.upload[index++] = level_duration_make(
            false,
            (uint32_t)subghz_protocol_dooya_const.te_long * 12 +
                subghz_protocol_dooya_const.te_short);
    }

    instance->encoder.upload[index++] =
        level_duration_make(true, (uint32_t)subghz_protocol_dooya_const.te_short * 13);
    instance->encoder.upload[index++] =
        level_duration_make(false, (uint32_t)subghz_protocol_dooya_const.te_long * 2);

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if(bit_read(instance->generic.data, i - 1)) {

            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_dooya_const.te_long);
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_dooya_const.te_short);
        } else {

            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_dooya_const.te_short);
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_dooya_const.te_long);
        }
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

void* subghz_protocol_encoder_dooya_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderDooya), &subghz_protocol_dooya, 3, 128);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_dooya_deserialize(void* context, FlipperFormat* flipper_format) {
    return subghz_protocol_encoder_common_deserialize(
        context, flipper_format, subghz_protocol_dooya_const.min_count_bit_for_found, (SubGhzProtocolEncoderGetUpload)subghz_protocol_encoder_dooya_get_upload);
}

static const SubGhzProtocolEncoder enc_dooya_vt = {
    .alloc = subghz_protocol_encoder_dooya_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_dooya_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_dooya = {
    .name = SUBGHZ_PROTOCOL_DOOYA_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_dooya_vt,
};

static const SubGhzEncodePluginEntry enc_dooya_entry = {.name = SUBGHZ_PROTOCOL_DOOYA_NAME, .encoder = &enc_dooya_vt};
static const FlipperAppPluginDescriptor enc_dooya_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_dooya_entry};
const FlipperAppPluginDescriptor* enc_dooya_ep(void) { return &enc_dooya_descriptor; }
