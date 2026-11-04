/* enc_doorbell32.fal : SD encoder for doorbell32 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_doorbell32 enc_doorbell32__subghz_protocol_doorbell32
#define subghz_protocol_encoder_common_alloc enc_doorbell32__subghz_protocol_encoder_common_alloc
#define subghz_protocol_encoder_common_free enc_doorbell32__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_doorbell32__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_doorbell32__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_doorbell32_alloc enc_doorbell32__subghz_protocol_encoder_doorbell32_alloc
#define subghz_protocol_encoder_doorbell32_deserialize enc_doorbell32__subghz_protocol_encoder_doorbell32_deserialize
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/doorbell32.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolDoorbell32"
#define DOORBELL32_GUARD_TIME 14u

struct SubGhzProtocolEncoderDoorbell32 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint32_t te;
};

static const SubGhzBlockConst subghz_protocol_doorbell32_const = {
    .te_short = 515,
    .te_long = 1545,
    .te_delta = 200,
    .min_count_bit_for_found = 32,
};

/**
 * Generating an upload from data.
 * @param instance Pointer to a SubGhzProtocolEncoderDoorbell32 instance
 * @return true Always; this encoder has no failure path
 */
static bool
    subghz_protocol_encoder_doorbell32_get_upload(SubGhzProtocolEncoderDoorbell32* instance);

/**
 * Generating an upload from data.
 * @param instance Pointer to a SubGhzProtocolEncoderDoorbell32 instance
 * @return true Always; this encoder has no failure path
 */
static bool
    subghz_protocol_encoder_doorbell32_get_upload(SubGhzProtocolEncoderDoorbell32* instance) {
    furi_assert(instance);

    size_t index = 0;
    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if(bit_read(instance->generic.data, i - 1)) {
            instance->encoder.upload[index++] = level_duration_make(true, instance->te * 3);
            instance->encoder.upload[index++] = level_duration_make(false, instance->te);
        } else {
            instance->encoder.upload[index++] = level_duration_make(true, instance->te);
            instance->encoder.upload[index++] = level_duration_make(false, instance->te * 3);
        }
    }
    // closing pulse, then the guard that separates the repeats
    instance->encoder.upload[index++] = level_duration_make(true, instance->te);
    instance->encoder.upload[index++] =
        level_duration_make(false, instance->te * DOORBELL32_GUARD_TIME);

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

void* subghz_protocol_encoder_doorbell32_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderDoorbell32), &subghz_protocol_doorbell32, 10, 68);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_doorbell32_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderDoorbell32* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_doorbell32_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) break;

        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        if(!flipper_format_read_uint32(flipper_format, "TE", &instance->te, 1)) {
            instance->te = subghz_protocol_doorbell32_const.te_short;
        }

        subghz_protocol_encoder_doorbell32_get_upload(instance);
        instance->encoder.is_running = true;
    } while(false);

    return ret;
}

static const SubGhzProtocolEncoder enc_doorbell32_vt = {
    .alloc = subghz_protocol_encoder_doorbell32_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_doorbell32_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_doorbell32 = {
    .name = SUBGHZ_PROTOCOL_DOORBELL32_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send |
            SubGhzProtocolFlag_Sensors,
    .decoder = NULL,
    .encoder = &enc_doorbell32_vt,
};

static const SubGhzEncodePluginEntry enc_doorbell32_entry = {.name = SUBGHZ_PROTOCOL_DOORBELL32_NAME, .encoder = &enc_doorbell32_vt};
static const FlipperAppPluginDescriptor enc_doorbell32_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_doorbell32_entry};
const FlipperAppPluginDescriptor* enc_doorbell32_ep(void) { return &enc_doorbell32_descriptor; }
