/* enc_intertechno_v3.fal : SD encoder for intertechno_v3 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_alloc enc_intertechno_v3__subghz_protocol_encoder_common_alloc
#define subghz_protocol_encoder_common_free enc_intertechno_v3__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_yield enc_intertechno_v3__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_intertechno_v3_alloc enc_intertechno_v3__subghz_protocol_encoder_intertechno_v3_alloc
#define subghz_protocol_encoder_intertechno_v3_deserialize enc_intertechno_v3__subghz_protocol_encoder_intertechno_v3_deserialize
#define subghz_protocol_encoder_intertechno_v3_stop enc_intertechno_v3__subghz_protocol_encoder_intertechno_v3_stop
#define subghz_protocol_intertechno_v3 enc_intertechno_v3__subghz_protocol_intertechno_v3
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/intertechno_v3.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolIntertechnoV3"
#define INTERTECHNO_V3_DIMMING_COUNT_BIT 36

struct SubGhzProtocolEncoderIntertechno_V3 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_intertechno_v3_const = {
    .te_short = 275,
    .te_long = 1375,
    .te_delta = 150,
    .min_count_bit_for_found = 32,
};

static bool subghz_protocol_encoder_intertechno_v3_get_upload(
    SubGhzProtocolEncoderIntertechno_V3* instance);

static bool subghz_protocol_encoder_intertechno_v3_get_upload(
    SubGhzProtocolEncoderIntertechno_V3* instance) {
    furi_assert(instance);
    size_t index = 0;

    instance->encoder.upload[index++] =
        level_duration_make(true, (uint32_t)subghz_protocol_intertechno_v3_const.te_short);
    instance->encoder.upload[index++] =
        level_duration_make(false, (uint32_t)subghz_protocol_intertechno_v3_const.te_short * 38);

    instance->encoder.upload[index++] =
        level_duration_make(true, (uint32_t)subghz_protocol_intertechno_v3_const.te_short);
    instance->encoder.upload[index++] =
        level_duration_make(false, (uint32_t)subghz_protocol_intertechno_v3_const.te_short * 10);

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if((instance->generic.data_count_bit == INTERTECHNO_V3_DIMMING_COUNT_BIT) && (i == 9)) {

            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_intertechno_v3_const.te_short);
            instance->encoder.upload[index++] = level_duration_make(
                false, (uint32_t)subghz_protocol_intertechno_v3_const.te_short);
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_intertechno_v3_const.te_short);
            instance->encoder.upload[index++] = level_duration_make(
                false, (uint32_t)subghz_protocol_intertechno_v3_const.te_short);
        } else if(bit_read(instance->generic.data, i - 1)) {

            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_intertechno_v3_const.te_short);
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_intertechno_v3_const.te_long);
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_intertechno_v3_const.te_short);
            instance->encoder.upload[index++] = level_duration_make(
                false, (uint32_t)subghz_protocol_intertechno_v3_const.te_short);
        } else {

            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_intertechno_v3_const.te_short);
            instance->encoder.upload[index++] = level_duration_make(
                false, (uint32_t)subghz_protocol_intertechno_v3_const.te_short);
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_intertechno_v3_const.te_short);
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_intertechno_v3_const.te_long);
        }
    }
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

void* subghz_protocol_encoder_intertechno_v3_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderIntertechno_V3), &subghz_protocol_intertechno_v3, 3, 256);
}

SubGhzProtocolStatus subghz_protocol_encoder_intertechno_v3_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderIntertechno_V3* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }
        if((instance->generic.data_count_bit !=
            subghz_protocol_intertechno_v3_const.min_count_bit_for_found) &&
           (instance->generic.data_count_bit != INTERTECHNO_V3_DIMMING_COUNT_BIT)) {
            FURI_LOG_E(TAG, "Wrong number of bits in key");
            ret = SubGhzProtocolStatusErrorValueBitCount;
            break;
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        if(!subghz_protocol_encoder_intertechno_v3_get_upload(instance)) {
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }
        instance->encoder.is_running = true;
    } while(false);

    return ret;
}

void subghz_protocol_encoder_intertechno_v3_stop(void* context) {
    SubGhzProtocolEncoderIntertechno_V3* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
}

static const SubGhzProtocolEncoder enc_intertechno_v3_vt = {
    .alloc = subghz_protocol_encoder_intertechno_v3_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_intertechno_v3_deserialize,
    .stop = subghz_protocol_encoder_intertechno_v3_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_intertechno_v3 = {
    .name = SUBGHZ_PROTOCOL_INTERTECHNO_V3_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_868 |
            SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_intertechno_v3_vt,
};

static const SubGhzEncodePluginEntry enc_intertechno_v3_entry = {.name = SUBGHZ_PROTOCOL_INTERTECHNO_V3_NAME, .encoder = &enc_intertechno_v3_vt};
static const FlipperAppPluginDescriptor enc_intertechno_v3_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_intertechno_v3_entry};
const FlipperAppPluginDescriptor* enc_intertechno_v3_ep(void) { return &enc_intertechno_v3_descriptor; }
