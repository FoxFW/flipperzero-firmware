/* decode_suzuki.fal : SD get_string for suzuki (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/suzuki.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>

typedef struct SubGhzProtocolDecoderSuzuki
{
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;
} SubGhzProtocolDecoderSuzuki;

static uint8_t suzuki_crc8(uint8_t* data, size_t len);
static uint8_t suzuki_calculate_crc(uint64_t data);
static uint8_t suzuki_custom_to_btn(uint8_t custom);
static const char *suzuki_get_button_name(uint8_t btn);

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

static const char *suzuki_get_button_name(uint8_t btn)
{
    switch (btn)
    {
    case 1: return "Panic";
    case 2: return "Trunk";
    case 3: return "Lock";
    case 4: return "Unlock";
    default: return "Unknown";
    }
}

static void suzuki_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderSuzuki* instance = input->decoder;

    
    uint64_t data = instance->generic.data;

    subghz_custom_btn_set_max(4);
    uint8_t selected = subghz_custom_btn_get() == SUBGHZ_CUSTOM_BTN_OK ?
                      subghz_custom_btn_get_original() :
                      subghz_custom_btn_get();
    uint8_t display_btn = suzuki_custom_to_btn(selected);
    if(display_btn != (uint8_t)((data >> 12) & 0xF)) {
        data = (data & ~((uint64_t)0xFULL << 12)) | ((uint64_t)(display_btn & 0xF) << 12);
        uint8_t new_crc = suzuki_calculate_crc(data);
        data = (data & ~((uint64_t)0xFFULL << 4)) | ((uint64_t)new_crc << 4);
    }

    uint32_t key_high = (data >> 32) & 0xFFFFFFFF;
    uint32_t key_low = data & 0xFFFFFFFF;
    uint8_t received_crc = (data >> 4) & 0xFF;
    
    uint8_t calculated_crc = suzuki_calculate_crc(data);
    bool crc_valid = (received_crc == calculated_crc);
    
    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Sn:%07lX Cnt:%04lX\r\n"
        "Btn:%02X:[%s]\r\n"
        "CRC:%02X %s",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        key_high,
        key_low,
        instance->generic.serial,
        instance->generic.cnt,
        display_btn,
        suzuki_get_button_name(display_btn),
        received_crc,
        crc_valid ? "(OK)" : "(FAIL)");
}

static const SubGhzDecodeEntry suzuki_items[] = {{.name = "SUZUKI", .get_string = suzuki_decode}};
static const SubGhzDecodeRegistry suzuki_registry = {.items = suzuki_items, .size = 1};
static const FlipperAppPluginDescriptor decode_suzuki_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &suzuki_registry};
const FlipperAppPluginDescriptor* decode_suzuki_ep(void) { return &decode_suzuki_descriptor; }
