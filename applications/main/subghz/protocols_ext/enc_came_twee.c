/* enc_came_twee.fal : SD encoder for came_twee (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_came_twee enc_came_twee__subghz_protocol_came_twee
#define subghz_protocol_encoder_came_twee_alloc enc_came_twee__subghz_protocol_encoder_came_twee_alloc
#define subghz_protocol_encoder_came_twee_deserialize enc_came_twee__subghz_protocol_encoder_came_twee_deserialize
#define subghz_protocol_encoder_came_twee_stop enc_came_twee__subghz_protocol_encoder_came_twee_stop
#define subghz_protocol_encoder_common_alloc enc_came_twee__subghz_protocol_encoder_common_alloc
#define subghz_protocol_encoder_common_free enc_came_twee__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_yield enc_came_twee__subghz_protocol_encoder_common_yield
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/came_twee.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

struct SubGhzProtocolEncoderCameTwee {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static const uint32_t came_twee_magic_numbers_xor[15] = {
    0x0E0E0E00,
    0x1D1D1D11,
    0x2C2C2C22,
    0x3B3B3B33,
    0x4A4A4A44,
    0x59595955,
    0x68686866,
    0x77777777,
    0x86868688,
    0x95959599,
    0xA4A4A4AA,
    0xB3B3B3BB,
    0xC2C2C2CC,
    0xD1D1D1DD,
    0xE0E0E0EE,
};

static const SubGhzBlockConst subghz_protocol_came_twee_const = {
    .te_short = 500,
    .te_long = 1000,
    .te_delta = 250,
    .min_count_bit_for_found = 54,
};

static LevelDuration
    subghz_protocol_encoder_came_twee_add_duration_to_upload(ManchesterEncoderResult result);
static void subghz_protocol_encoder_came_twee_get_upload(SubGhzProtocolEncoderCameTwee* instance);
static void subghz_protocol_came_twee_remote_controller(SubGhzBlockGeneric* instance);

static LevelDuration
    subghz_protocol_encoder_came_twee_add_duration_to_upload(ManchesterEncoderResult result) {
    LevelDuration data = {.duration = 0, .level = 0};
    switch(result) {
    case ManchesterEncoderResultShortLow:
        data.duration = subghz_protocol_came_twee_const.te_short;
        data.level = false;
        break;
    case ManchesterEncoderResultLongLow:
        data.duration = subghz_protocol_came_twee_const.te_long;
        data.level = false;
        break;
    case ManchesterEncoderResultLongHigh:
        data.duration = subghz_protocol_came_twee_const.te_long;
        data.level = true;
        break;
    case ManchesterEncoderResultShortHigh:
        data.duration = subghz_protocol_came_twee_const.te_short;
        data.level = true;
        break;

    default:
        furi_crash("SubGhz: ManchesterEncoderResult is incorrect.");
        break;
    }
    return level_duration_make(data.level, data.duration);
}

static void subghz_protocol_encoder_came_twee_get_upload(SubGhzProtocolEncoderCameTwee* instance) {
    furi_assert(instance);
    size_t index = 0;

    ManchesterEncoderState enc_state;
    manchester_encoder_reset(&enc_state);
    ManchesterEncoderResult result;

    uint64_t temp_parcel = 0x003FFF7200000000;

    for(int i = 14; i >= 0; i--) {
        temp_parcel = (temp_parcel & 0xFFFFFFFF00000000) |
                      (instance->generic.serial ^ came_twee_magic_numbers_xor[i]);

        for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
            if(!manchester_encoder_advance(&enc_state, !bit_read(temp_parcel, i - 1), &result)) {
                instance->encoder.upload[index++] =
                    subghz_protocol_encoder_came_twee_add_duration_to_upload(result);
                manchester_encoder_advance(&enc_state, !bit_read(temp_parcel, i - 1), &result);
            }
            instance->encoder.upload[index++] =
                subghz_protocol_encoder_came_twee_add_duration_to_upload(result);
        }
        instance->encoder.upload[index] = subghz_protocol_encoder_came_twee_add_duration_to_upload(
            manchester_encoder_finish(&enc_state));
        if(level_duration_get_level(instance->encoder.upload[index])) {
            index++;
        }
        instance->encoder.upload[index++] =
            level_duration_make(false, (uint32_t)subghz_protocol_came_twee_const.te_long * 51);
    }
    instance->encoder.size_upload = index;
}

static void subghz_protocol_came_twee_remote_controller(SubGhzBlockGeneric* instance) {

    uint8_t cnt_parcel = (uint8_t)(instance->data & 0xF);
    uint32_t data = (uint32_t)(instance->data & 0x0FFFFFFFF);

    data = (data ^ came_twee_magic_numbers_xor[cnt_parcel]);
    instance->serial = data;
    data /= 4;
    instance->btn = (data >> 4) & 0x0F;
    data >>= 16;
    data = (uint16_t)subghz_protocol_blocks_reverse_key(data, 16);
    instance->cnt = data >> 6;
}

/* vendored from lib/subghz/protocols/common.c (not exported) */
void subghz_protocol_encoder_common_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderCommon* instance = context;
    free(instance->encoder.upload);
    free(instance);
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

void* subghz_protocol_encoder_came_twee_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderCameTwee), &subghz_protocol_came_twee, 3, 1536);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_came_twee_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderCameTwee* instance = context;
    SubGhzProtocolStatus res = SubGhzProtocolStatusError;
    do {
        res = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_came_twee_const.min_count_bit_for_found);
        if(res != SubGhzProtocolStatusOk) {
            break;
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        subghz_protocol_came_twee_remote_controller(&instance->generic);
        subghz_protocol_encoder_came_twee_get_upload(instance);
        instance->encoder.front = 0;
        instance->encoder.is_running = true;
    } while(false);

    return res;
}

void subghz_protocol_encoder_came_twee_stop(void* context) {
    SubGhzProtocolEncoderCameTwee* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
}

static const SubGhzProtocolEncoder enc_came_twee_vt = {
    .alloc = subghz_protocol_encoder_came_twee_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_came_twee_deserialize,
    .stop = subghz_protocol_encoder_came_twee_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_came_twee = {
    .name = SUBGHZ_PROTOCOL_CAME_TWEE_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_came_twee_vt,
};

static const SubGhzEncodePluginEntry enc_came_twee_entry = {.name = SUBGHZ_PROTOCOL_CAME_TWEE_NAME, .encoder = &enc_came_twee_vt};
static const FlipperAppPluginDescriptor enc_came_twee_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_came_twee_entry};
const FlipperAppPluginDescriptor* enc_came_twee_ep(void) { return &enc_came_twee_descriptor; }
