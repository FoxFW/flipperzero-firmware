/* enc_holtek_ht12x.fal : SD encoder for holtek_ht12x (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_alloc enc_holtek_ht12x__subghz_protocol_encoder_common_alloc
#define subghz_protocol_encoder_common_free enc_holtek_ht12x__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_holtek_ht12x__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_holtek_ht12x__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_holtek_th12x_alloc enc_holtek_ht12x__subghz_protocol_encoder_holtek_th12x_alloc
#define subghz_protocol_encoder_holtek_th12x_deserialize enc_holtek_ht12x__subghz_protocol_encoder_holtek_th12x_deserialize
#define subghz_protocol_holtek_th12x enc_holtek_ht12x__subghz_protocol_holtek_th12x
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/holtek_ht12x.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolHoltekHt12x"

struct SubGhzProtocolEncoderHoltek_HT12X {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint32_t te;
};

static const SubGhzBlockConst subghz_protocol_holtek_th12x_const = {
    .te_short = 320,
    .te_long = 640,
    .te_delta = 200,
    .min_count_bit_for_found = 12,
};

static bool
    subghz_protocol_encoder_holtek_th12x_get_upload(SubGhzProtocolEncoderHoltek_HT12X* instance);

static bool
    subghz_protocol_encoder_holtek_th12x_get_upload(SubGhzProtocolEncoderHoltek_HT12X* instance) {
    furi_assert(instance);

    size_t index = 0;
    size_t size_upload = (instance->generic.data_count_bit * 2) + 2;
    if(size_upload > instance->encoder.size_upload) {
        FURI_LOG_E(TAG, "Size upload exceeds allocated encoder buffer.");
        return false;
    } else {
        instance->encoder.size_upload = size_upload;
    }

    instance->encoder.upload[index++] = level_duration_make(false, (uint32_t)instance->te * 36);

    instance->encoder.upload[index++] = level_duration_make(true, (uint32_t)instance->te);

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if(bit_read(instance->generic.data, i - 1)) {

            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)instance->te * 2);
            instance->encoder.upload[index++] = level_duration_make(true, (uint32_t)instance->te);
        } else {

            instance->encoder.upload[index++] = level_duration_make(false, (uint32_t)instance->te);
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)instance->te * 2);
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

void* subghz_protocol_encoder_holtek_th12x_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderHoltek_HT12X), &subghz_protocol_holtek_th12x, 3, 128);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_holtek_th12x_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderHoltek_HT12X* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_holtek_th12x_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        if(!flipper_format_read_uint32(flipper_format, "TE", (uint32_t*)&instance->te, 1)) {
            FURI_LOG_E(TAG, "Missing TE");
            ret = SubGhzProtocolStatusErrorParserTe;
            break;
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        if(!subghz_protocol_encoder_holtek_th12x_get_upload(instance)) {
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }
        instance->encoder.is_running = true;
    } while(false);

    return ret;
}

static const SubGhzProtocolEncoder enc_holtek_ht12x_vt = {
    .alloc = subghz_protocol_encoder_holtek_th12x_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_holtek_th12x_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_holtek_th12x = {
    .name = SUBGHZ_PROTOCOL_HOLTEK_HT12X_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 | SubGhzProtocolFlag_315 |
            SubGhzProtocolFlag_AM | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_holtek_ht12x_vt,
};

static const SubGhzEncodePluginEntry enc_holtek_ht12x_entry = {.name = SUBGHZ_PROTOCOL_HOLTEK_HT12X_NAME, .encoder = &enc_holtek_ht12x_vt};
static const FlipperAppPluginDescriptor enc_holtek_ht12x_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_holtek_ht12x_entry};
const FlipperAppPluginDescriptor* enc_holtek_ht12x_ep(void) { return &enc_holtek_ht12x_descriptor; }
