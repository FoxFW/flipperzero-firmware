/* enc_treadmill37.fal : SD encoder for treadmill37 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_alloc enc_treadmill37__subghz_protocol_encoder_common_alloc
#define subghz_protocol_encoder_common_free enc_treadmill37__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_treadmill37__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_treadmill37__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_treadmill37_alloc enc_treadmill37__subghz_protocol_encoder_treadmill37_alloc
#define subghz_protocol_encoder_treadmill37_deserialize enc_treadmill37__subghz_protocol_encoder_treadmill37_deserialize
#define subghz_protocol_treadmill37 enc_treadmill37__subghz_protocol_treadmill37
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/treadmill37.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

struct SubGhzProtocolEncoderTreadmill37 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_treadmill37_const = {
    .te_short = 300,
    .te_long = 900,
    .te_delta = 150,
    .min_count_bit_for_found = 37,
};

static void
    subghz_protocol_encoder_treadmill37_get_upload(SubGhzProtocolEncoderTreadmill37* instance);
static void subghz_protocol_treadmill37_check_remote_controller(SubGhzBlockGeneric* instance);

static void
    subghz_protocol_encoder_treadmill37_get_upload(SubGhzProtocolEncoderTreadmill37* instance) {
    furi_assert(instance);
    size_t index = 0;

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if(bit_read(instance->generic.data, i - 1)) {

            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_treadmill37_const.te_long);
            if(i == 1) {

                instance->encoder.upload[index++] = level_duration_make(
                    false, (uint32_t)subghz_protocol_treadmill37_const.te_short * 20);
            } else {
                instance->encoder.upload[index++] = level_duration_make(
                    false, (uint32_t)subghz_protocol_treadmill37_const.te_short);
            }
        } else {

            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_treadmill37_const.te_short);
            if(i == 1) {

                instance->encoder.upload[index++] = level_duration_make(
                    false, (uint32_t)subghz_protocol_treadmill37_const.te_short * 20);
            } else {
                instance->encoder.upload[index++] = level_duration_make(
                    false, (uint32_t)subghz_protocol_treadmill37_const.te_long);
            }
        }
    }

    instance->encoder.size_upload = index;
    return;
}

static void subghz_protocol_treadmill37_check_remote_controller(SubGhzBlockGeneric* instance) {
    instance->serial = instance->data >> 17;
    instance->cnt = (instance->data >> 1) & 0xFFFF;
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

void* subghz_protocol_encoder_treadmill37_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderTreadmill37), &subghz_protocol_treadmill37, 3, 128);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_treadmill37_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderTreadmill37* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_treadmill37_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        subghz_protocol_treadmill37_check_remote_controller(&instance->generic);
        subghz_protocol_encoder_treadmill37_get_upload(instance);
        instance->encoder.is_running = true;
    } while(false);

    return ret;
}

static const SubGhzProtocolEncoder enc_treadmill37_vt = {
    .alloc = subghz_protocol_encoder_treadmill37_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_treadmill37_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_treadmill37 = {
    .name = SUBGHZ_PROTOCOL_TREADMILL37_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_treadmill37_vt,
};

static const SubGhzEncodePluginEntry enc_treadmill37_entry = {.name = SUBGHZ_PROTOCOL_TREADMILL37_NAME, .encoder = &enc_treadmill37_vt};
static const FlipperAppPluginDescriptor enc_treadmill37_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_treadmill37_entry};
const FlipperAppPluginDescriptor* enc_treadmill37_ep(void) { return &enc_treadmill37_descriptor; }
