/* enc_legrand.fal : SD encoder for legrand (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_free enc_legrand__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_legrand__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_legrand__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_legrand_alloc enc_legrand__subghz_protocol_encoder_legrand_alloc
#define subghz_protocol_encoder_legrand_deserialize enc_legrand__subghz_protocol_encoder_legrand_deserialize
#define subghz_protocol_legrand enc_legrand__subghz_protocol_legrand
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/legrand.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolLegrand"

struct SubGhzProtocolEncoderLegrand {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint32_t te;
};

static const SubGhzBlockConst subghz_protocol_legrand_const = {
    .te_short = 375,
    .te_long = 1125,
    .te_delta = 150,
    .min_count_bit_for_found = 18,
};

static bool subghz_protocol_encoder_legrand_get_upload(SubGhzProtocolEncoderLegrand* instance);

static bool subghz_protocol_encoder_legrand_get_upload(SubGhzProtocolEncoderLegrand* instance) {
    furi_assert(instance);

    size_t size_upload = (instance->generic.data_count_bit * 2) + 1;
    if(size_upload != instance->encoder.size_upload) {
        FURI_LOG_E(TAG, "Invalid data bit count");
        return false;
    }

    size_t index = 0;

    instance->encoder.upload[index++] = level_duration_make(false, (uint32_t)instance->te * 16);

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if(bit_read(instance->generic.data, i - 1)) {

            instance->encoder.upload[index++] = level_duration_make(false, (uint32_t)instance->te);
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)instance->te * 3);
        } else {

            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)instance->te * 3);
            instance->encoder.upload[index++] = level_duration_make(true, (uint32_t)instance->te);
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

void* subghz_protocol_encoder_legrand_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderLegrand* instance = malloc(sizeof(SubGhzProtocolEncoderLegrand));

    instance->base.protocol = &subghz_protocol_legrand;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 3;
    instance->encoder.size_upload = subghz_protocol_legrand_const.min_count_bit_for_found * 2 + 1;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_legrand_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderLegrand* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_legrand_const.min_count_bit_for_found);
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

        if(!subghz_protocol_encoder_legrand_get_upload(instance)) {
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }
        instance->encoder.is_running = true;
    } while(false);

    return ret;
}

static const SubGhzProtocolEncoder enc_legrand_vt = {
    .alloc = subghz_protocol_encoder_legrand_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_legrand_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_legrand = {
    .name = SUBGHZ_PROTOCOL_LEGRAND_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send |
            SubGhzProtocolFlag_Sensors,
    .decoder = NULL,
    .encoder = &enc_legrand_vt,
};

static const SubGhzEncodePluginEntry enc_legrand_entry = {.name = SUBGHZ_PROTOCOL_LEGRAND_NAME, .encoder = &enc_legrand_vt};
static const FlipperAppPluginDescriptor enc_legrand_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_legrand_entry};
const FlipperAppPluginDescriptor* enc_legrand_ep(void) { return &enc_legrand_descriptor; }
