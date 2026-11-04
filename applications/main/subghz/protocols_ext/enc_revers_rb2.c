/* enc_revers_rb2.fal : SD encoder for revers_rb2 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_alloc enc_revers_rb2__subghz_protocol_encoder_common_alloc
#define subghz_protocol_encoder_common_free enc_revers_rb2__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_revers_rb2__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_revers_rb2__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_revers_rb2_alloc enc_revers_rb2__subghz_protocol_encoder_revers_rb2_alloc
#define subghz_protocol_encoder_revers_rb2_deserialize enc_revers_rb2__subghz_protocol_encoder_revers_rb2_deserialize
#define subghz_protocol_revers_rb2 enc_revers_rb2__subghz_protocol_revers_rb2
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/revers_rb2.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

struct SubGhzProtocolEncoderRevers_RB2 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_revers_rb2_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 160,
    .min_count_bit_for_found = 64,
};

static LevelDuration
    subghz_protocol_encoder_revers_rb2_add_duration_to_upload(ManchesterEncoderResult result);
static void
    subghz_protocol_encoder_revers_rb2_get_upload(SubGhzProtocolEncoderRevers_RB2* instance);
static void subghz_protocol_revers_rb2_remote_controller(SubGhzBlockGeneric* instance);

static LevelDuration
    subghz_protocol_encoder_revers_rb2_add_duration_to_upload(ManchesterEncoderResult result) {
    LevelDuration data = {.duration = 0, .level = 0};
    switch(result) {
    case ManchesterEncoderResultShortLow:
        data.duration = subghz_protocol_revers_rb2_const.te_short;
        data.level = false;
        break;
    case ManchesterEncoderResultLongLow:
        data.duration = subghz_protocol_revers_rb2_const.te_long;
        data.level = false;
        break;
    case ManchesterEncoderResultLongHigh:
        data.duration = subghz_protocol_revers_rb2_const.te_long;
        data.level = true;
        break;
    case ManchesterEncoderResultShortHigh:
        data.duration = subghz_protocol_revers_rb2_const.te_short;
        data.level = true;
        break;

    default:
        furi_crash("SubGhz: ManchesterEncoderResult is incorrect.");
        break;
    }
    return level_duration_make(data.level, data.duration);
}

static void
    subghz_protocol_encoder_revers_rb2_get_upload(SubGhzProtocolEncoderRevers_RB2* instance) {
    furi_assert(instance);
    size_t index = 0;

    ManchesterEncoderState enc_state;
    manchester_encoder_reset(&enc_state);
    ManchesterEncoderResult result;

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if(!manchester_encoder_advance(
               &enc_state, bit_read(instance->generic.data, i - 1), &result)) {
            instance->encoder.upload[index++] =
                subghz_protocol_encoder_revers_rb2_add_duration_to_upload(result);
            manchester_encoder_advance(
                &enc_state, bit_read(instance->generic.data, i - 1), &result);
        }
        instance->encoder.upload[index++] =
            subghz_protocol_encoder_revers_rb2_add_duration_to_upload(result);
    }
    instance->encoder.upload[index] = subghz_protocol_encoder_revers_rb2_add_duration_to_upload(
        manchester_encoder_finish(&enc_state));
    if(level_duration_get_level(instance->encoder.upload[index])) {
        index++;
    }
    instance->encoder.upload[index++] = level_duration_make(false, (uint32_t)320);
    instance->encoder.size_upload = index;
}

static void subghz_protocol_revers_rb2_remote_controller(SubGhzBlockGeneric* instance) {

    instance->serial = (((instance->data << 16) >> 16) >> 10);
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

void* subghz_protocol_encoder_revers_rb2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderRevers_RB2), &subghz_protocol_revers_rb2, 3, 256);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_revers_rb2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderRevers_RB2* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_revers_rb2_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        subghz_protocol_revers_rb2_remote_controller(&instance->generic);
        subghz_protocol_encoder_revers_rb2_get_upload(instance);
        instance->encoder.is_running = true;
    } while(false);

    return ret;
}

static const SubGhzProtocolEncoder enc_revers_rb2_vt = {
    .alloc = subghz_protocol_encoder_revers_rb2_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_revers_rb2_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_revers_rb2 = {
    .name = SUBGHZ_PROTOCOL_REVERSRB2_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send |
            SubGhzProtocolFlag_ReversRB2,
    .decoder = NULL,
    .encoder = &enc_revers_rb2_vt,
};

static const SubGhzEncodePluginEntry enc_revers_rb2_entry = {.name = SUBGHZ_PROTOCOL_REVERSRB2_NAME, .encoder = &enc_revers_rb2_vt};
static const FlipperAppPluginDescriptor enc_revers_rb2_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_revers_rb2_entry};
const FlipperAppPluginDescriptor* enc_revers_rb2_ep(void) { return &enc_revers_rb2_descriptor; }
