/* decode_kia_v5.fal : SD get_string for kia_v5 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/kia_v5.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/toolbox/manchester_decoder.h>

struct SubGhzProtocolDecoderKiaV5 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;

    ManchesterState manchester_state;
    uint64_t decoded_data;
    uint64_t saved_key;
    uint8_t bit_count;
    uint64_t yek;
    uint8_t crc;
};

static uint8_t kia_v5_custom_to_btn(uint8_t custom_btn_id, uint8_t original_btn);
static uint8_t kia_v5_calculate_crc(uint64_t data);
static const char* subghz_protocol_kia_v5_get_name_button(uint8_t btn);

static uint8_t kia_v5_custom_to_btn(uint8_t custom_btn_id, uint8_t original_btn) {
    switch(custom_btn_id) {
    case SUBGHZ_CUSTOM_BTN_UP:
        return 0x02U;
    case SUBGHZ_CUSTOM_BTN_DOWN:
        return 0x01U;
    case SUBGHZ_CUSTOM_BTN_LEFT:
        return 0x04U;
    case SUBGHZ_CUSTOM_BTN_RIGHT:
        return 0x08U;
    case SUBGHZ_CUSTOM_BTN_OK:
    default:
        return original_btn;
    }
}

static uint8_t kia_v5_calculate_crc(uint64_t data) {
    uint8_t crc = 0;
    for(int i = 63; i >= 0; i--) {
        const uint8_t bit = (data >> i) & 1U;
        const uint8_t shifted_out = (crc >> 1U) & 1U;
        crc = (uint8_t)(((crc & 1U) << 1U) | bit);
        if(shifted_out) {
            crc ^= 3U;
        }
    }
    return (uint8_t)(crc & 3U);
}

static const char* subghz_protocol_kia_v5_get_name_button(uint8_t btn) {
    switch(btn) {
    case 0x01:
        return "Unlock";
    case 0x02:
        return "Lock";
    case 0x04:
        return "Trunk";
    case 0x08:
        return "Horn";
    default:
        return "Unknown";
    }
}

static void kia_v5_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderKiaV5* instance = input->decoder;


    uint8_t calculated_crc = kia_v5_calculate_crc(instance->generic.data);
    bool crc_valid = (instance->crc == calculated_crc);

    subghz_custom_btn_set_max(4);
    uint8_t display_btn = (uint8_t)instance->generic.btn;
    uint8_t custom_btn_id = subghz_custom_btn_get();
    if(custom_btn_id != SUBGHZ_CUSTOM_BTN_OK) {
        display_btn = kia_v5_custom_to_btn(custom_btn_id, (uint8_t)(instance->generic.btn & 0x0FU));
    }

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%llX\r\n"
        "SN:0x%07lX Btn:[%s]\r\n"
        "CRC:%u Cnt:%04lX %s",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long long)instance->generic.data,
        (unsigned long)instance->generic.serial,
        subghz_protocol_kia_v5_get_name_button(display_btn),
        (unsigned)instance->crc,
        (unsigned long)instance->generic.cnt,
        crc_valid ? "(OK)" : "(FAIL)");
}

static const SubGhzDecodeEntry kia_v5_items[] = {{.name = "KIA/HYU V5", .get_string = kia_v5_decode}};
static const SubGhzDecodeRegistry kia_v5_registry = {.items = kia_v5_items, .size = 1};
static const FlipperAppPluginDescriptor decode_kia_v5_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &kia_v5_registry};
const FlipperAppPluginDescriptor* decode_kia_v5_ep(void) { return &decode_kia_v5_descriptor; }
