/* enc_ansonic.fal : SD encoder for ansonic (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_ansonic enc_ansonic__subghz_protocol_ansonic
#define subghz_protocol_encoder_ansonic_alloc enc_ansonic__subghz_protocol_encoder_ansonic_alloc
#define subghz_protocol_encoder_ansonic_deserialize enc_ansonic__subghz_protocol_encoder_ansonic_deserialize
#define subghz_protocol_encoder_common_alloc enc_ansonic__subghz_protocol_encoder_common_alloc
#define subghz_protocol_encoder_common_free enc_ansonic__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_ansonic__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_ansonic__subghz_protocol_encoder_common_yield
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/ansonic.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolAnsonic"

struct SubGhzProtocolEncoderAnsonic {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_ansonic_const = {
    .te_short = 555,
    .te_long = 1111,
    .te_delta = 120,
    .min_count_bit_for_found = 12,
};

static bool subghz_protocol_encoder_ansonic_get_upload(SubGhzProtocolEncoderAnsonic* instance);

static bool subghz_protocol_encoder_ansonic_get_upload(SubGhzProtocolEncoderAnsonic* instance) {
    furi_assert(instance);
    size_t index = 0;
    size_t size_upload = (instance->generic.data_count_bit * 2) + 2;
    if(size_upload > instance->encoder.size_upload) {
        FURI_LOG_E(TAG, "Size upload exceeds allocated encoder buffer.");
        return false;
    } else {
        instance->encoder.size_upload = size_upload;
    }

    instance->encoder.upload[index++] =
        level_duration_make(false, (uint32_t)subghz_protocol_ansonic_const.te_short * 35);

    instance->encoder.upload[index++] =
        level_duration_make(true, (uint32_t)subghz_protocol_ansonic_const.te_short);

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if(bit_read(instance->generic.data, i - 1)) {

            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_ansonic_const.te_short);
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_ansonic_const.te_long);
        } else {

            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_ansonic_const.te_long);
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_ansonic_const.te_short);
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

void* subghz_protocol_encoder_ansonic_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderAnsonic), &subghz_protocol_ansonic, 3, 52);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_ansonic_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderAnsonic* instance = context;
    SubGhzProtocolStatus res = SubGhzProtocolStatusError;
    do {
        res = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_ansonic_const.min_count_bit_for_found);
        if(res != SubGhzProtocolStatusOk) {
            FURI_LOG_E(TAG, "Deserialize error");
            break;
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        if(!subghz_protocol_encoder_ansonic_get_upload(instance)) {
            res = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }
        instance->encoder.is_running = true;
    } while(false);

    return res;
}

static const SubGhzProtocolEncoder enc_ansonic_vt = {
    .alloc = subghz_protocol_encoder_ansonic_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_ansonic_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_ansonic = {
    .name = SUBGHZ_PROTOCOL_ANSONIC_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_FM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_ansonic_vt,
};

static const SubGhzEncodePluginEntry enc_ansonic_entry = {.name = SUBGHZ_PROTOCOL_ANSONIC_NAME, .encoder = &enc_ansonic_vt};
static const FlipperAppPluginDescriptor enc_ansonic_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_ansonic_entry};
const FlipperAppPluginDescriptor* enc_ansonic_ep(void) { return &enc_ansonic_descriptor; }
