/* enc_feron.fal : SD encoder for feron (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_alloc enc_feron__subghz_protocol_encoder_common_alloc
#define subghz_protocol_encoder_common_free enc_feron__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_feron__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_feron__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_feron_alloc enc_feron__subghz_protocol_encoder_feron_alloc
#define subghz_protocol_encoder_feron_deserialize enc_feron__subghz_protocol_encoder_feron_deserialize
#define subghz_protocol_feron enc_feron__subghz_protocol_feron
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/feron.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

struct SubGhzProtocolEncoderFeron {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_feron_const = {
    .te_short = 350,
    .te_long = 750,
    .te_delta = 150,
    .min_count_bit_for_found = 32,
};

static void subghz_protocol_encoder_feron_get_upload(SubGhzProtocolEncoderFeron* instance);
static void subghz_protocol_feron_check_remote_controller(SubGhzBlockGeneric* instance);

static void subghz_protocol_encoder_feron_get_upload(SubGhzProtocolEncoderFeron* instance) {
    furi_assert(instance);
    size_t index = 0;

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if(bit_read(instance->generic.data, i - 1)) {

            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_feron_const.te_long);
            if(i == 1) {

                instance->encoder.upload[index++] = level_duration_make(
                    false, (uint32_t)subghz_protocol_feron_const.te_short + 150);
                instance->encoder.upload[index++] = level_duration_make(
                    true, (uint32_t)subghz_protocol_feron_const.te_short + 150);

                instance->encoder.upload[index++] =
                    level_duration_make(false, (uint32_t)subghz_protocol_feron_const.te_long * 6);
            } else {
                instance->encoder.upload[index++] =
                    level_duration_make(false, (uint32_t)subghz_protocol_feron_const.te_short);
            }
        } else {

            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_feron_const.te_short);
            if(i == 1) {

                instance->encoder.upload[index++] = level_duration_make(
                    false, (uint32_t)subghz_protocol_feron_const.te_short + 150);
                instance->encoder.upload[index++] = level_duration_make(
                    true, (uint32_t)subghz_protocol_feron_const.te_short + 150);

                instance->encoder.upload[index++] =
                    level_duration_make(false, (uint32_t)subghz_protocol_feron_const.te_long * 6);
            } else {
                instance->encoder.upload[index++] =
                    level_duration_make(false, (uint32_t)subghz_protocol_feron_const.te_long);
            }
        }
    }

    instance->encoder.size_upload = index;
    return;
}

static void subghz_protocol_feron_check_remote_controller(SubGhzBlockGeneric* instance) {
    instance->serial = instance->data >> 16;
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

void* subghz_protocol_encoder_feron_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderFeron), &subghz_protocol_feron, 3, 256);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_feron_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderFeron* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_feron_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        subghz_protocol_feron_check_remote_controller(&instance->generic);
        subghz_protocol_encoder_feron_get_upload(instance);
        instance->encoder.is_running = true;
    } while(false);

    return ret;
}

static const SubGhzProtocolEncoder enc_feron_vt = {
    .alloc = subghz_protocol_encoder_feron_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_feron_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_feron = {
    .name = SUBGHZ_PROTOCOL_FERON_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send |
            SubGhzProtocolFlag_Sensors,
    .decoder = NULL,
    .encoder = &enc_feron_vt,
};

static const SubGhzEncodePluginEntry enc_feron_entry = {.name = SUBGHZ_PROTOCOL_FERON_NAME, .encoder = &enc_feron_vt};
static const FlipperAppPluginDescriptor enc_feron_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_feron_entry};
const FlipperAppPluginDescriptor* enc_feron_ep(void) { return &enc_feron_descriptor; }
