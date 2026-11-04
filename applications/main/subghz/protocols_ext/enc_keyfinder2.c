/* enc_keyfinder2.fal : SD encoder for keyfinder2 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_alloc enc_keyfinder2__subghz_protocol_encoder_common_alloc
#define subghz_protocol_encoder_common_deserialize enc_keyfinder2__subghz_protocol_encoder_common_deserialize
#define subghz_protocol_encoder_common_free enc_keyfinder2__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_keyfinder2__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_keyfinder2__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_keyfinder2_alloc enc_keyfinder2__subghz_protocol_encoder_keyfinder2_alloc
#define subghz_protocol_encoder_keyfinder2_deserialize enc_keyfinder2__subghz_protocol_encoder_keyfinder2_deserialize
#define subghz_protocol_keyfinder2 enc_keyfinder2__subghz_protocol_keyfinder2
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/keyfinder2.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

#define KEYFINDER2_TE_WIDE_SHORT 1500
#define KEYFINDER2_TE_WIDE_LONG  2500
#define KEYFINDER2_HEADER_BITS   3

struct SubGhzProtocolEncoderKeyFinder2 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_keyfinder2_const = {
    .te_short = 350,
    .te_long = 1020,
    .te_delta = 150,
    .min_count_bit_for_found = 11,
};

/**
 * Generating an upload from data.
 * @param context Pointer to a SubGhzProtocolEncoderKeyFinder2 instance
 * @return true Always; this encoder has no failure path
 */
static bool subghz_protocol_encoder_keyfinder2_get_upload(void* context);

/**
 * Generating an upload from data.
 * @param context Pointer to a SubGhzProtocolEncoderKeyFinder2 instance
 * @return true Always; this encoder has no failure path
 */
static bool subghz_protocol_encoder_keyfinder2_get_upload(void* context) {
    SubGhzProtocolEncoderKeyFinder2* instance = context;
    furi_assert(instance);

    size_t index = 0;
    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        bool wide = (i > instance->generic.data_count_bit - KEYFINDER2_HEADER_BITS);
        uint32_t te_short = wide ? KEYFINDER2_TE_WIDE_SHORT :
                                   subghz_protocol_keyfinder2_const.te_short;
        uint32_t te_long = wide ? KEYFINDER2_TE_WIDE_LONG :
                                  subghz_protocol_keyfinder2_const.te_long;

        if(bit_read(instance->generic.data, i - 1)) {
            instance->encoder.upload[index++] = level_duration_make(true, te_short);
            instance->encoder.upload[index++] = level_duration_make(false, te_long);
        } else {
            instance->encoder.upload[index++] = level_duration_make(true, te_long);
            instance->encoder.upload[index++] = level_duration_make(false, te_short);
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

/**
 * Colour printed on the button for this code. Taken from one four button fob, so a fob
 * with a different button layout may well name them differently.
 * @param btn Button code, 3 bit
 * @return Colour name
 */
void* subghz_protocol_encoder_keyfinder2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderKeyFinder2), &subghz_protocol_keyfinder2, 5, 32);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_keyfinder2_deserialize(void* context, FlipperFormat* flipper_format) {
    return subghz_protocol_encoder_common_deserialize(
        context,
        flipper_format,
        subghz_protocol_keyfinder2_const.min_count_bit_for_found,
        subghz_protocol_encoder_keyfinder2_get_upload);
}

static const SubGhzProtocolEncoder enc_keyfinder2_vt = {
    .alloc = subghz_protocol_encoder_keyfinder2_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_keyfinder2_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_keyfinder2 = {
    .name = SUBGHZ_PROTOCOL_KEYFINDER2_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send |
            SubGhzProtocolFlag_Sensors,
    .decoder = NULL,
    .encoder = &enc_keyfinder2_vt,
};

static const SubGhzEncodePluginEntry enc_keyfinder2_entry = {.name = SUBGHZ_PROTOCOL_KEYFINDER2_NAME, .encoder = &enc_keyfinder2_vt};
static const FlipperAppPluginDescriptor enc_keyfinder2_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_keyfinder2_entry};
const FlipperAppPluginDescriptor* enc_keyfinder2_ep(void) { return &enc_keyfinder2_descriptor; }
