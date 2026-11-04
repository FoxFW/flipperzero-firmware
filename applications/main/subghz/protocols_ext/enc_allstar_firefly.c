/* enc_allstar_firefly.fal : SD encoder for allstar_firefly (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_allstar_firefly enc_allstar_firefly__subghz_protocol_allstar_firefly
#define subghz_protocol_encoder_allstar_firefly_alloc enc_allstar_firefly__subghz_protocol_encoder_allstar_firefly_alloc
#define subghz_protocol_encoder_allstar_firefly_deserialize enc_allstar_firefly__subghz_protocol_encoder_allstar_firefly_deserialize
#define subghz_protocol_encoder_common_free enc_allstar_firefly__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_allstar_firefly__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_allstar_firefly__subghz_protocol_encoder_common_yield
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/allstar_firefly.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

#define ALLSTAR_FIREFLY_UPLOAD_SIZE 256

struct SubGhzProtocolEncoderAllstarFirefly {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_allstar_firefly_const = {
    .te_short = 600,
    .te_long = 4000,
    .te_delta = 300,
    .min_count_bit_for_found = 18,
};

static bool
    subghz_protocol_encoder_allstar_firefly_get_upload(SubGhzProtocolEncoderAllstarFirefly* instance);

static bool
    subghz_protocol_encoder_allstar_firefly_get_upload(SubGhzProtocolEncoderAllstarFirefly* instance) {
    furi_assert(instance);
    size_t index = 0;
    uint32_t te_short = subghz_protocol_allstar_firefly_const.te_short;
    uint32_t te_long = subghz_protocol_allstar_firefly_const.te_long;
    uint32_t gap = te_short * 50 + 400;

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        bool bit = bit_read(instance->generic.data, i - 1);
        if(bit) {
            instance->encoder.upload[index++] = level_duration_make(true, te_long);
            instance->encoder.upload[index++] =
                level_duration_make(false, (i == 1) ? gap : te_short);
        } else {
            instance->encoder.upload[index++] = level_duration_make(true, te_short);
            instance->encoder.upload[index++] =
                level_duration_make(false, (i == 1) ? gap : te_long);
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

void* subghz_protocol_encoder_allstar_firefly_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderAllstarFirefly* instance =
        malloc(sizeof(SubGhzProtocolEncoderAllstarFirefly));

    instance->base.protocol = &subghz_protocol_allstar_firefly;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 5;
    instance->encoder.size_upload = ALLSTAR_FIREFLY_UPLOAD_SIZE;
    instance->encoder.upload = malloc(ALLSTAR_FIREFLY_UPLOAD_SIZE * sizeof(LevelDuration));
    instance->encoder.front = 0;
    instance->encoder.is_running = false;
    return instance;
}

SubGhzProtocolStatus subghz_protocol_encoder_allstar_firefly_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderAllstarFirefly* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_allstar_firefly_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) break;

        if(!subghz_protocol_encoder_allstar_firefly_get_upload(instance)) {
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }
        instance->encoder.front = 0;
        instance->encoder.is_running = true;
    } while(false);
    return ret;
}

static const SubGhzProtocolEncoder enc_allstar_firefly_vt = {
    .alloc = subghz_protocol_encoder_allstar_firefly_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_allstar_firefly_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_allstar_firefly = {
    .name = SUBGHZ_PROTOCOL_ALLSTAR_FIREFLY_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_allstar_firefly_vt,
};

static const SubGhzEncodePluginEntry enc_allstar_firefly_entry = {.name = SUBGHZ_PROTOCOL_ALLSTAR_FIREFLY_NAME, .encoder = &enc_allstar_firefly_vt};
static const FlipperAppPluginDescriptor enc_allstar_firefly_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_allstar_firefly_entry};
const FlipperAppPluginDescriptor* enc_allstar_firefly_ep(void) { return &enc_allstar_firefly_descriptor; }
