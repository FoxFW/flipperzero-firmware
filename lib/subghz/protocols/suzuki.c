#include "suzuki.h"

#include "common.h"
#include "../encode_plugin.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"

#define TAG "SuzukiProtocol"

static const SubGhzBlockConst subghz_protocol_suzuki_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 99,
    .min_count_bit_for_found = 64,
};

#define SUZUKI_GAP_TIME 2000
#define SUZUKI_GAP_DELTA 399
#define SUZUKI_MIN_PREAMBLE_COUNT 200
#define SUZUKI_ENCODER_PREAMBLE_COUNT 300

typedef struct SubGhzProtocolDecoderSuzuki
{
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;
} SubGhzProtocolDecoderSuzuki;

typedef struct SubGhzProtocolEncoderSuzuki
{
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
} SubGhzProtocolEncoderSuzuki;

typedef enum
{
    SuzukiDecoderStepReset = 0,
    SuzukiDecoderStepCountPreamble = 1,
    SuzukiDecoderStepDecodeData = 2,
} SuzukiDecoderStep;

static void suzuki_add_bit(SubGhzProtocolDecoderSuzuki* instance, uint8_t bit) {
    instance->decoder.decode_data = (instance->decoder.decode_data << 1) | bit;
    instance->decoder.decode_count_bit++;
}

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

static uint8_t suzuki_btn_to_custom(uint8_t btn) {
    switch(btn) {
        case 1: return 4;
        case 2: return 3;
        case 3: return 1;
        case 4: return 2;
        default: return 2;
    }
}

static bool suzuki_verify_crc(uint64_t data) {
    uint8_t received_crc = (data >> 4) & 0xFF;
    uint8_t calculated_crc = suzuki_calculate_crc(data);
    return (received_crc == calculated_crc);
}

const SubGhzProtocolDecoder subghz_protocol_suzuki_decoder = {
    .alloc = subghz_protocol_decoder_suzuki_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_suzuki_feed,
    .reset = subghz_protocol_decoder_common_reset,
    .get_hash_data = subghz_protocol_decoder_suzuki_get_hash_data,
    .serialize = subghz_protocol_decoder_suzuki_serialize,
    .deserialize = subghz_protocol_decoder_suzuki_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderSuzuki);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderSuzuki);

static const SubGhzEncoderExternal subghz_protocol_suzuki_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_suzuki");

const SubGhzProtocol subghz_protocol_suzuki = {
    .name = SUBGHZ_PROTOCOL_SUZUKI_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_FM | 
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_suzuki_decoder,
    .encoder = &subghz_protocol_suzuki_encoder_ext.vtable,
};

void* subghz_protocol_decoder_suzuki_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderSuzuki), &subghz_protocol_suzuki);
}

void subghz_protocol_decoder_suzuki_feed(void *context, bool level, uint32_t duration)
{
    furi_assert(context);
    SubGhzProtocolDecoderSuzuki *instance = context;

    switch (instance->decoder.parser_step)
    {
    case SuzukiDecoderStepReset:
        if (!level) return;
        if (DURATION_DIFF(duration, subghz_protocol_suzuki_const.te_short) > subghz_protocol_suzuki_const.te_delta) return;
        
        instance->decoder.decode_data = 0;
        instance->decoder.decode_count_bit = 0;
        instance->decoder.parser_step = SuzukiDecoderStepCountPreamble;
        instance->header_count = 0;
        break;

    case SuzukiDecoderStepCountPreamble:
        if (level)
        {
            if (instance->header_count >= SUZUKI_MIN_PREAMBLE_COUNT)
            {
                if (DURATION_DIFF(duration, subghz_protocol_suzuki_const.te_long) <= subghz_protocol_suzuki_const.te_delta)
                {
                    instance->decoder.parser_step = SuzukiDecoderStepDecodeData;
                    suzuki_add_bit(instance, 1);
                }
            }
        }
        else
        {
            if (DURATION_DIFF(duration, subghz_protocol_suzuki_const.te_short) <= subghz_protocol_suzuki_const.te_delta)
            {
                instance->decoder.te_last = duration;
                instance->header_count++;
            }
            else
            {
                instance->decoder.parser_step = SuzukiDecoderStepReset;
            }
        }
        break;

    case SuzukiDecoderStepDecodeData:
        if (level)
        {
            if (duration < subghz_protocol_suzuki_const.te_long)
            {
                uint32_t diff_long = 500 - duration;
                if (diff_long > 99)
                {
                    uint32_t diff_short = (duration < 250) ? (250 - duration) : (duration - 250);
                    if (diff_short <= 99) suzuki_add_bit(instance, 0);
                }
                else
                {
                    suzuki_add_bit(instance, 1);
                }
            }
            else
            {
                uint32_t diff_long = duration - 500;
                if (diff_long <= 99) suzuki_add_bit(instance, 1);
            }
        }
        else
        {
            uint32_t diff_gap = (duration < SUZUKI_GAP_TIME) ? (SUZUKI_GAP_TIME - duration) : (duration - SUZUKI_GAP_TIME);
            
            if (diff_gap <= SUZUKI_GAP_DELTA)
            {
                if (instance->decoder.decode_count_bit == 64)
                {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = 64;

                    if (suzuki_verify_crc(instance->generic.data))
                    {
                        uint64_t data = instance->generic.data;
                        uint32_t data_high = (uint32_t)(data >> 32);
                        uint32_t data_low = (uint32_t)data;
                        
                        instance->generic.serial = ((data_high & 0xFFF) << 16) | (data_low >> 16);
                        instance->generic.btn = (data_low >> 12) & 0xF;
                        instance->generic.cnt = (data_high << 4) >> 16;

                        if(subghz_custom_btn_get_original() == 0) {
                            uint8_t custom = suzuki_btn_to_custom(instance->generic.btn);
                            subghz_custom_btn_set_original(custom);
                        }
                        subghz_custom_btn_set_max(4);

                        if (instance->base.callback)
                        {
                            instance->base.callback(&instance->base, instance->base.context);
                        }
                    }
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->decoder.parser_step = SuzukiDecoderStepReset;
            }
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_suzuki_get_hash_data(void *context)
{
    furi_assert(context);
    SubGhzProtocolDecoderSuzuki *instance = context;
    return subghz_protocol_blocks_get_hash_data(&instance->decoder, (instance->generic.data_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_suzuki_serialize(void *context, FlipperFormat *flipper_format, SubGhzRadioPreset *preset)
{
    furi_assert(context);
    SubGhzProtocolDecoderSuzuki *instance = context;
    
    uint32_t temp_serial = instance->generic.serial;
    uint32_t temp_cnt = instance->generic.cnt;
    uint32_t temp_btn = instance->generic.btn;
    
    instance->generic.serial = 0;
    instance->generic.cnt = 0;
    instance->generic.btn = 0;
    
    SubGhzProtocolStatus ret = subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    
    instance->generic.serial = temp_serial;
    instance->generic.cnt = temp_cnt;
    instance->generic.btn = temp_btn;
    
    return ret;
    if(ret == SubGhzProtocolStatusOk) {
        flipper_format_write_uint32(flipper_format, "Serial", &temp_serial, 1);
        flipper_format_write_uint32(flipper_format, "Btn", &temp_btn, 1);
        flipper_format_write_uint32(flipper_format, "Cnt", &temp_cnt, 1);
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_suzuki_deserialize(void *context, FlipperFormat *flipper_format)
{
    furi_assert(context);
    SubGhzProtocolDecoderSuzuki *instance = context;
    
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
    
    if(ret == SubGhzProtocolStatusOk) {
        uint64_t data = instance->generic.data;
        instance->generic.cnt = (uint32_t)((data >> 44) & 0xFFFFF);
        instance->generic.serial = (uint32_t)((data >> 16) & 0x0FFFFFFF);
        instance->generic.btn = (uint8_t)((data >> 12) & 0xF);
        
        if(subghz_custom_btn_get_original() == 0) {
            uint8_t custom = suzuki_btn_to_custom(instance->generic.btn);
            subghz_custom_btn_set_original(custom);
        }
        subghz_custom_btn_set_max(4);
    }
    
    return ret;
}

