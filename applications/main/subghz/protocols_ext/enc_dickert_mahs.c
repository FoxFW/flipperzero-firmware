/* enc_dickert_mahs.fal : SD encoder for dickert_mahs (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_dickert_mahs enc_dickert_mahs__subghz_protocol_dickert_mahs
#define subghz_protocol_encoder_common_alloc enc_dickert_mahs__subghz_protocol_encoder_common_alloc
#define subghz_protocol_encoder_common_free enc_dickert_mahs__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_dickert_mahs__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_dickert_mahs__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_dickert_mahs_alloc enc_dickert_mahs__subghz_protocol_encoder_dickert_mahs_alloc
#define subghz_protocol_encoder_dickert_mahs_deserialize enc_dickert_mahs__subghz_protocol_encoder_dickert_mahs_deserialize
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/dickert_mahs.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_rtc.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolDicketMAHS"

struct SubGhzProtocolEncoderDickertMAHS {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_dickert_mahs_const = {
    .te_short = 400,
    .te_long = 800,
    .te_delta = 100,
    .min_count_bit_for_found = 36,
};

static bool
    subghz_protocol_encoder_dickert_mahs_get_upload(SubGhzProtocolEncoderDickertMAHS* instance);

static bool
    subghz_protocol_encoder_dickert_mahs_get_upload(SubGhzProtocolEncoderDickertMAHS* instance) {
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
        level_duration_make(false, (uint32_t)subghz_protocol_dickert_mahs_const.te_short * 112);

    instance->encoder.upload[index++] =
        level_duration_make(true, (uint32_t)subghz_protocol_dickert_mahs_const.te_short);

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if(bit_read(instance->generic.data, i - 1)) {

            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_dickert_mahs_const.te_long);
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_dickert_mahs_const.te_short);
        } else {

            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_dickert_mahs_const.te_short);
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_dickert_mahs_const.te_long);
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

void* subghz_protocol_encoder_dickert_mahs_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderDickertMAHS), &subghz_protocol_dickert_mahs, 3, 128);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_dickert_mahs_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderDickertMAHS* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }

        if((instance->generic.data_count_bit <
            subghz_protocol_dickert_mahs_const.min_count_bit_for_found)) {
            FURI_LOG_E(TAG, "Wrong number of bits in key");
            ret = SubGhzProtocolStatusErrorValueBitCount;
            break;
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        if(!subghz_protocol_encoder_dickert_mahs_get_upload(instance)) {
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }
        instance->encoder.is_running = true;
    } while(false);

    return ret;
}

static const SubGhzProtocolEncoder enc_dickert_mahs_vt = {
    .alloc = subghz_protocol_encoder_dickert_mahs_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_dickert_mahs_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_dickert_mahs = {
    .name = SUBGHZ_PROTOCOL_DICKERT_MAHS_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_dickert_mahs_vt,
};

static const SubGhzEncodePluginEntry enc_dickert_mahs_entry = {.name = SUBGHZ_PROTOCOL_DICKERT_MAHS_NAME, .encoder = &enc_dickert_mahs_vt};
static const FlipperAppPluginDescriptor enc_dickert_mahs_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_dickert_mahs_entry};
const FlipperAppPluginDescriptor* enc_dickert_mahs_ep(void) { return &enc_dickert_mahs_descriptor; }
