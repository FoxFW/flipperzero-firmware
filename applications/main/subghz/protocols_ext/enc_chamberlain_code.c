/* enc_chamberlain_code.fal : SD encoder for chamberlain_code (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_chamb_code enc_chamberlain_code__subghz_protocol_chamb_code
#define subghz_protocol_encoder_chamb_code_alloc enc_chamberlain_code__subghz_protocol_encoder_chamb_code_alloc
#define subghz_protocol_encoder_chamb_code_deserialize enc_chamberlain_code__subghz_protocol_encoder_chamb_code_deserialize
#define subghz_protocol_encoder_common_alloc enc_chamberlain_code__subghz_protocol_encoder_common_alloc
#define subghz_protocol_encoder_common_free enc_chamberlain_code__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_chamberlain_code__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_chamberlain_code__subghz_protocol_encoder_common_yield
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/chamberlain_code.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolChambCode"
#define CHAMBERLAIN_CODE_BIT_1    0b0011
#define CHAMBERLAIN_CODE_BIT_0    0b0111
#define CHAMBERLAIN_7_CODE_MASK_CHECK 0x10000001101
#define CHAMBERLAIN_8_CODE_MASK_CHECK 0x1000001001
#define CHAMBERLAIN_9_CODE_MASK_CHECK 0x10000000001
#define UPLOAD_HEX_DATA_SIZE 10

struct SubGhzProtocolEncoderChamb_Code {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_chamb_code_const = {
    .te_short = 1000,
    .te_long = 3000,
    .te_delta = 200,
    .min_count_bit_for_found = 10,
};

static uint64_t subghz_protocol_chamb_bit_to_code(uint64_t data, uint8_t size);
static bool
    subghz_protocol_encoder_chamb_code_get_upload(SubGhzProtocolEncoderChamb_Code* instance);

static uint64_t subghz_protocol_chamb_bit_to_code(uint64_t data, uint8_t size) {
    uint64_t data_res = 0;
    for(uint8_t i = 0; i < size; i++) {
        if(!(bit_read(data, size - i - 1))) {
            data_res = data_res << 4 | CHAMBERLAIN_CODE_BIT_0;
        } else {
            data_res = data_res << 4 | CHAMBERLAIN_CODE_BIT_1;
        }
    }
    return data_res;
}

static bool
    subghz_protocol_encoder_chamb_code_get_upload(SubGhzProtocolEncoderChamb_Code* instance) {
    furi_assert(instance);

    uint64_t data = subghz_protocol_chamb_bit_to_code(
        instance->generic.data, instance->generic.data_count_bit);

    switch(instance->generic.data_count_bit) {
    case 7:
        data = ((data >> 4) << 16) | (data & 0xF) << 4 | CHAMBERLAIN_7_CODE_MASK_CHECK;
        break;
    case 8:
        data = ((data >> 12) << 16) | (data & 0xFF) << 4 | CHAMBERLAIN_8_CODE_MASK_CHECK;
        break;
    case 9:
        data = (data << 4) | CHAMBERLAIN_9_CODE_MASK_CHECK;
        break;

    default:
        FURI_LOG_E(TAG, "Invalid bits count");
        return false;
        break;
    }
#define UPLOAD_HEX_DATA_SIZE 10
    uint8_t upload_hex_data[UPLOAD_HEX_DATA_SIZE] = {0};
    size_t upload_hex_count_bit = 0;

    for(uint8_t i = 0; i < 36; i++) {
        subghz_protocol_blocks_set_bit_array(
            0, upload_hex_data, upload_hex_count_bit++, UPLOAD_HEX_DATA_SIZE);
    }

    switch(instance->generic.data_count_bit) {
    case 7:
    case 9:
        for(uint8_t i = 44; i > 0; i--) {
            if(!bit_read(data, i - 1)) {
                subghz_protocol_blocks_set_bit_array(
                    0, upload_hex_data, upload_hex_count_bit++, UPLOAD_HEX_DATA_SIZE);
            } else {
                subghz_protocol_blocks_set_bit_array(
                    1, upload_hex_data, upload_hex_count_bit++, UPLOAD_HEX_DATA_SIZE);
            }
        }
        break;
    case 8:
        for(uint8_t i = 40; i > 0; i--) {
            if(!bit_read(data, i - 1)) {
                subghz_protocol_blocks_set_bit_array(
                    0, upload_hex_data, upload_hex_count_bit++, UPLOAD_HEX_DATA_SIZE);
            } else {
                subghz_protocol_blocks_set_bit_array(
                    1, upload_hex_data, upload_hex_count_bit++, UPLOAD_HEX_DATA_SIZE);
            }
        }
        break;
    }

    instance->encoder.size_upload = subghz_protocol_blocks_get_upload_from_bit_array(
        upload_hex_data,
        upload_hex_count_bit,
        instance->encoder.upload,
        instance->encoder.size_upload,
        subghz_protocol_chamb_code_const.te_short,
        SubGhzProtocolBlockAlignBitLeft);

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

void* subghz_protocol_encoder_chamb_code_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderChamb_Code), &subghz_protocol_chamb_code, 3, 24);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_chamb_code_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderChamb_Code* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }
        if(instance->generic.data_count_bit >
           subghz_protocol_chamb_code_const.min_count_bit_for_found) {
            FURI_LOG_E(TAG, "Wrong number of bits in key");
            ret = SubGhzProtocolStatusErrorValueBitCount;
            break;
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        if(!subghz_protocol_encoder_chamb_code_get_upload(instance)) {
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }
        instance->encoder.is_running = true;

    } while(false);

    return ret;
}

static const SubGhzProtocolEncoder enc_chamberlain_code_vt = {
    .alloc = subghz_protocol_encoder_chamb_code_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_chamb_code_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_chamb_code = {
    .name = SUBGHZ_PROTOCOL_CHAMB_CODE_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_chamberlain_code_vt,
};

static const SubGhzEncodePluginEntry enc_chamberlain_code_entry = {.name = SUBGHZ_PROTOCOL_CHAMB_CODE_NAME, .encoder = &enc_chamberlain_code_vt};
static const FlipperAppPluginDescriptor enc_chamberlain_code_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_chamberlain_code_entry};
const FlipperAppPluginDescriptor* enc_chamberlain_code_ep(void) { return &enc_chamberlain_code_descriptor; }
