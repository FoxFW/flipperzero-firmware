/* decode_kia_v1.fal : SD get_string for kia_v1 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/kia_v1.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/toolbox/manchester_decoder.h>

struct SubGhzProtocolDecoderKiaV1 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t header_count;
    ManchesterState manchester_saved_state;
    uint8_t crc;
    bool crc_check;
};

static uint8_t kia_v1_crc4(const uint8_t* bytes, int count, uint8_t offset);
static void subghz_protocol_kia_v1_check_remote_controller(SubGhzProtocolDecoderKiaV1* instance);
static uint8_t kia_v1_ui_button(uint8_t custom, uint8_t original_btn);
static const char* subghz_protocol_kia_v1_get_name_button(uint8_t btn);

static uint8_t kia_v1_crc4(const uint8_t* bytes, int count, uint8_t offset) {
    uint8_t crc = 0;

    for(int i = 0; i < count; i++) {
        uint8_t b = bytes[i];
        crc ^= ((b & 0x0F) ^ (b >> 4));
    }

    crc = (crc + offset) & 0x0F;
    return crc;
}

static void subghz_protocol_kia_v1_check_remote_controller(SubGhzProtocolDecoderKiaV1* instance) {
    instance->generic.serial = instance->generic.data >> 24;
    instance->generic.btn = (instance->generic.data >> 16) & 0xFF;
    instance->generic.cnt = ((instance->generic.data >> 4) & 0xF) << 8 |
                            ((instance->generic.data >> 8) & 0xFF);

    uint8_t cnt_high = (instance->generic.cnt >> 8) & 0xF;
    uint8_t char_data[7];
    char_data[0] = (instance->generic.serial >> 24) & 0xFF;
    char_data[1] = (instance->generic.serial >> 16) & 0xFF;
    char_data[2] = (instance->generic.serial >> 8) & 0xFF;
    char_data[3] = instance->generic.serial & 0xFF;
    char_data[4] = instance->generic.btn;
    char_data[5] = instance->generic.cnt & 0xFF;

    char_data[6] = cnt_high;
    uint8_t crc = kia_v1_crc4(char_data, 7, 1);

    instance->crc = cnt_high << 4 | crc;
    instance->crc_check = (crc == (instance->generic.data & 0xF));
}

static uint8_t kia_v1_ui_button(uint8_t custom, uint8_t original_btn) {
    switch(custom) {
    case SUBGHZ_CUSTOM_BTN_UP:
        return 0x1U;
    case SUBGHZ_CUSTOM_BTN_DOWN:
        return 0x2U;
    case SUBGHZ_CUSTOM_BTN_LEFT:
        return 0x3U;
    case SUBGHZ_CUSTOM_BTN_RIGHT:
    case SUBGHZ_CUSTOM_BTN_OK:
    default:
        return original_btn;
    }
}

static const char* subghz_protocol_kia_v1_get_name_button(uint8_t btn) {
    const char* name;
    switch(btn) {
    case 0x1:
        name = "Close";
        break;
    case 0x2:
        name = "Open";
        break;
    case 0x3:
        name = "Boot";
        break;
    default:
        name = "??";
        break;
    }
    return name;
}

static void kia_v1_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderKiaV1* instance = input->decoder;


    subghz_protocol_kia_v1_check_remote_controller(instance);

    subghz_custom_btn_set_max(4);
    uint8_t display_btn = (uint8_t)instance->generic.btn;
    uint8_t custom_btn_id = subghz_custom_btn_get();
    if(custom_btn_id != SUBGHZ_CUSTOM_BTN_OK) {
        display_btn = kia_v1_ui_button(custom_btn_id, (uint8_t)(instance->generic.btn & 0x0FU));
    }

    uint64_t display_data = instance->generic.data;
    uint8_t display_crc = instance->crc;
    bool display_crc_ok = instance->crc_check;
    if(display_btn != (uint8_t)instance->generic.btn) {
        display_data = (display_data & ~((uint64_t)0xFFULL << 16)) |
                       ((uint64_t)display_btn << 16);

        uint32_t serial = (uint32_t)(display_data >> 24);
        uint16_t cnt = (uint16_t)((((display_data >> 4) & 0xF) << 8) |
                                  ((display_data >> 8) & 0xFF));
        uint8_t cnt_high = (cnt >> 8) & 0xF;
        uint8_t char_data[7];
        char_data[0] = (serial >> 24) & 0xFF;
        char_data[1] = (serial >> 16) & 0xFF;
        char_data[2] = (serial >> 8) & 0xFF;
        char_data[3] = serial & 0xFF;
        char_data[4] = display_btn;
        char_data[5] = cnt & 0xFF;
        char_data[6] = cnt_high;
        display_crc = kia_v1_crc4(char_data, 7, 1);
        display_data = (display_data & ~((uint64_t)0xFULL)) | (display_crc & 0xF);
        display_crc_ok = true;
    }

    uint32_t code_found_hi = display_data >> 32;
    uint32_t code_found_lo = display_data & 0xFFFFFFFF;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%06lX%08lX\r\n"
        "Serial:%08lX\r\n"
        "Cnt:%03lX CRC:%01X %s\r\n"
        "Btn:%02X:%s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        code_found_hi,
        code_found_lo,
        instance->generic.serial,
        instance->generic.cnt,
        display_crc,
        display_crc_ok ? "OK" : "WRONG",
        display_btn,
        subghz_protocol_kia_v1_get_name_button(display_btn));
}

static const SubGhzDecodeEntry kia_v1_items[] = {{.name = "KIA/HYU V1", .get_string = kia_v1_decode}};
static const SubGhzDecodeRegistry kia_v1_registry = {.items = kia_v1_items, .size = 1};
static const FlipperAppPluginDescriptor decode_kia_v1_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &kia_v1_registry};
const FlipperAppPluginDescriptor* decode_kia_v1_ep(void) { return &decode_kia_v1_descriptor; }
