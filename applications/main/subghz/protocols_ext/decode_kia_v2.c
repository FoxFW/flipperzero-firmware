/* decode_kia_v2.fal : SD get_string for kia_v2 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/kia_v2.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/toolbox/manchester_encoder.h>
#include <furi.h>

struct SubGhzProtocolDecoderKiaV2 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;

    ManchesterState manchester_state;
};

static uint8_t kia_v2_calculate_crc(uint64_t data);
static uint8_t kia_v2_ui_button(uint8_t custom, uint8_t original_btn);

static uint8_t kia_v2_calculate_crc(uint64_t data) {
    uint64_t data_without_crc = data >> 4;

    uint8_t bytes[6];
    bytes[0] = (uint8_t)(data_without_crc);
    bytes[1] = (uint8_t)(data_without_crc >> 8);
    bytes[2] = (uint8_t)(data_without_crc >> 16);
    bytes[3] = (uint8_t)(data_without_crc >> 24);
    bytes[4] = (uint8_t)(data_without_crc >> 32);
    bytes[5] = (uint8_t)(data_without_crc >> 40);

    uint8_t crc = 0;
    for(int i = 0; i < 6; i++) {
        crc ^= (bytes[i] & 0x0F) ^ (bytes[i] >> 4);
    }

    return (crc + 1) & 0x0F;
}

static uint8_t kia_v2_ui_button(uint8_t custom, uint8_t original_btn) {
    switch(custom) {
    case SUBGHZ_CUSTOM_BTN_UP:
        return 0x01U;
    case SUBGHZ_CUSTOM_BTN_DOWN:
        return 0x02U;
    case SUBGHZ_CUSTOM_BTN_LEFT:
        return 0x03U;
    case SUBGHZ_CUSTOM_BTN_RIGHT:
        return 0x04U;
    case SUBGHZ_CUSTOM_BTN_OK:
    default:
        return original_btn;
    }
}

static void kia_v2_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderKiaV2* instance = input->decoder;


    subghz_custom_btn_set_max(4);
    uint8_t original_btn = (uint8_t)(instance->generic.btn & 0x0FU);
    uint8_t display_btn = original_btn;
    uint8_t custom_btn_id = subghz_custom_btn_get();
    if(custom_btn_id != SUBGHZ_CUSTOM_BTN_OK) {
        display_btn = kia_v2_ui_button(custom_btn_id, original_btn);
    }

    uint64_t display_data = instance->generic.data;
    if(display_btn != original_btn) {
        display_data = (display_data & ~((uint64_t)0x0FULL << 16)) |
                       ((uint64_t)(display_btn & 0x0FU) << 16);
        uint8_t new_crc = kia_v2_calculate_crc(display_data);
        display_data = (display_data & ~((uint64_t)0x0FULL)) | (new_crc & 0x0FU);
    }

    uint8_t crc = display_data & 0x0F;

    bool crc_valid = crc == kia_v2_calculate_crc(display_data);

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%013llX\r\n"
        "Sn:%08lX Btn:%X\r\n"
        "Cnt:%03lX CRC:%X - %s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        display_data,
        instance->generic.serial,
        display_btn,
        instance->generic.cnt,
        crc,
        crc_valid ? "OK" : "BAD");
}

static const SubGhzDecodeEntry kia_v2_items[] = {{.name = "KIA/HYU V2", .get_string = kia_v2_decode}};
static const SubGhzDecodeRegistry kia_v2_registry = {.items = kia_v2_items, .size = 1};
static const FlipperAppPluginDescriptor decode_kia_v2_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &kia_v2_registry};
const FlipperAppPluginDescriptor* decode_kia_v2_ep(void) { return &decode_kia_v2_descriptor; }
