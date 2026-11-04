/* enc_suzuki.fal : SD encoder for suzuki (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_stop enc_suzuki__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_suzuki__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_suzuki_alloc enc_suzuki__subghz_protocol_encoder_suzuki_alloc
#define subghz_protocol_encoder_suzuki_deserialize enc_suzuki__subghz_protocol_encoder_suzuki_deserialize
#define subghz_protocol_encoder_suzuki_free enc_suzuki__subghz_protocol_encoder_suzuki_free
#define subghz_protocol_suzuki enc_suzuki__subghz_protocol_suzuki
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/suzuki.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/common.h>

#define SUZUKI_GAP_TIME 2000
#define SUZUKI_ENCODER_PREAMBLE_COUNT 300

typedef struct SubGhzProtocolEncoderSuzuki
{
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
} SubGhzProtocolEncoderSuzuki;

static const SubGhzBlockConst subghz_protocol_suzuki_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 99,
    .min_count_bit_for_found = 64,
};

static uint8_t suzuki_crc8(uint8_t* data, size_t len);
static uint8_t suzuki_calculate_crc(uint64_t data);
static uint8_t suzuki_custom_to_btn(uint8_t custom);
static uint8_t suzuki_btn_to_custom(uint8_t btn);

static uint8_t suzuki_crc8(uint8_t* data, size_t len) {
    uint8_t crc = 0x00;
    for(size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for(size_t j = 0; j < 8; j++) {
            if((crc & 0x80) != 0)
                crc = (uint8_t)((crc << 1) ^ 0x7F);
            else
                crc <<= 1;
        }
    }
    return crc;
}

static uint8_t suzuki_calculate_crc(uint64_t data) {
    uint8_t crc_data[6];
    crc_data[0] = (data >> 52) & 0xFF;
    crc_data[1] = (data >> 44) & 0xFF;
    crc_data[2] = (data >> 36) & 0xFF;
    crc_data[3] = (data >> 28) & 0xFF;
    crc_data[4] = (data >> 20) & 0xFF;
    crc_data[5] = (data >> 12) & 0xFF;
    return suzuki_crc8(crc_data, 6);
}

static uint8_t suzuki_custom_to_btn(uint8_t custom) {
    switch(custom) {
        case 1: return 3;
        case 2: return 4;
        case 3: return 2;
        case 4: return 1;
        default: return 4;
    }
}

static uint8_t suzuki_btn_to_custom(uint8_t btn) {
    switch(btn) {
        case 1: return 4;
        case 2: return 3;
        case 3: return 1;
        case 4: return 2;
        default: return 2;
    }
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

void *subghz_protocol_encoder_suzuki_alloc(SubGhzEnvironment *environment)
{
    UNUSED(environment);
    SubGhzProtocolEncoderSuzuki *instance = malloc(sizeof(SubGhzProtocolEncoderSuzuki));
    instance->base.protocol = &subghz_protocol_suzuki;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.upload = NULL;
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_suzuki_free(void *context)
{
    furi_assert(context);
    SubGhzProtocolEncoderSuzuki *instance = context;
    if(instance->encoder.upload) {
        free(instance->encoder.upload);
    }
    free(instance);
}

SubGhzProtocolStatus subghz_protocol_encoder_suzuki_deserialize(void *context, FlipperFormat *flipper_format)
{
    furi_assert(context);
    SubGhzProtocolEncoderSuzuki *instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if (ret != SubGhzProtocolStatusOk) break;

        uint64_t data = instance->generic.data;
        
        instance->generic.cnt = (uint32_t)((data >> 44) & 0xFFFFF);
        instance->generic.serial = (uint32_t)((data >> 16) & 0x0FFFFFFF);
        instance->generic.btn = (uint8_t)((data >> 12) & 0xF);

        if(subghz_custom_btn_get_original() == 0) {
            uint8_t custom = suzuki_btn_to_custom(instance->generic.btn);
            subghz_custom_btn_set_original(custom);
        }
        subghz_custom_btn_set_max(4);

        uint32_t override_cnt = 0;
        if(subghz_block_generic_global_counter_override_get(&override_cnt)) {
            instance->generic.cnt = override_cnt & 0xFFFFF;
        } else {
            uint32_t mult = furi_hal_subghz_get_rolling_counter_mult();
            instance->generic.cnt = (instance->generic.cnt + mult) & 0xFFFFF;
        }

        uint8_t selected = subghz_custom_btn_get() == SUBGHZ_CUSTOM_BTN_OK ?
                          subghz_custom_btn_get_original() :
                          subghz_custom_btn_get();
        uint8_t btn = suzuki_custom_to_btn(selected);
        subghz_block_generic_global_button_override_get(&btn);
        instance->generic.btn = btn;

        uint64_t new_data = 0;
        new_data |= ((uint64_t)(instance->generic.cnt & 0xFFFFF) << 44);
        new_data |= ((uint64_t)(instance->generic.serial & 0x0FFFFFFF) << 16);
        new_data |= ((uint64_t)(instance->generic.btn & 0xF) << 12);
        
        uint8_t crc = suzuki_calculate_crc(new_data);
        new_data |= ((uint64_t)crc << 4);
        
        instance->generic.data = new_data;

        if(!flipper_format_rewind(flipper_format)) {
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }

        uint8_t key_data[8];
        for(size_t i = 0; i < 8; i++) {
            key_data[i] = (instance->generic.data >> (56 - i * 8)) & 0xFF;
        }

        if(!flipper_format_update_hex(flipper_format, "Key", key_data, 8)) {
            ret = SubGhzProtocolStatusErrorParserKey;
            break;
        }

        uint32_t temp_btn = instance->generic.btn;
        flipper_format_rewind(flipper_format);
        flipper_format_insert_or_update_uint32(flipper_format, "Btn", &temp_btn, 1);

        flipper_format_rewind(flipper_format);
        flipper_format_insert_or_update_uint32(flipper_format, "Cnt", &instance->generic.cnt, 1);

        size_t preamble_count = SUZUKI_ENCODER_PREAMBLE_COUNT;
        size_t bit_count = 64;
        instance->encoder.size_upload = (preamble_count * 2) + (bit_count * 2) + 1;

        if(instance->encoder.upload) {
            free(instance->encoder.upload);
        }
        instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));

        size_t index = 0;

        for (size_t i = 0; i < preamble_count; i++) {
            instance->encoder.upload[index++] = level_duration_make(true, subghz_protocol_suzuki_const.te_short);
            instance->encoder.upload[index++] = level_duration_make(false, subghz_protocol_suzuki_const.te_short);
        }

        for (size_t i = 0; i < bit_count; i++) {
            uint8_t bit = (instance->generic.data >> (63 - i)) & 1;
            
            if (bit) {
                instance->encoder.upload[index++] = level_duration_make(true, subghz_protocol_suzuki_const.te_long);
            } else {
                instance->encoder.upload[index++] = level_duration_make(true, subghz_protocol_suzuki_const.te_short);
            }
            instance->encoder.upload[index++] = level_duration_make(false, subghz_protocol_suzuki_const.te_short);
        }

        instance->encoder.upload[index++] = level_duration_make(false, SUZUKI_GAP_TIME);

        instance->encoder.is_running = true;
        instance->encoder.repeat = 5;
        instance->encoder.front = 0;

        ret = SubGhzProtocolStatusOk;

    } while (false);

    return ret;
}

static const SubGhzProtocolEncoder enc_suzuki_vt = {
    .alloc = subghz_protocol_encoder_suzuki_alloc,
    .free = subghz_protocol_encoder_suzuki_free,
    .deserialize = subghz_protocol_encoder_suzuki_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_suzuki = {
    .name = SUBGHZ_PROTOCOL_SUZUKI_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_FM | 
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_suzuki_vt,
};

static const SubGhzEncodePluginEntry enc_suzuki_entry = {.name = SUBGHZ_PROTOCOL_SUZUKI_NAME, .encoder = &enc_suzuki_vt};
static const FlipperAppPluginDescriptor enc_suzuki_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_suzuki_entry};
const FlipperAppPluginDescriptor* enc_suzuki_ep(void) { return &enc_suzuki_descriptor; }
