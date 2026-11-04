/* decode_hay21.fal : SD get_string for hay21 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/hay21.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>

struct SubGhzProtocolDecoderHay21 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static void subghz_protocol_hay21_remote_controller(SubGhzBlockGeneric* instance);
static const char* subghz_protocol_hay21_get_button_name(uint8_t btn);

static void subghz_protocol_hay21_remote_controller(SubGhzBlockGeneric* instance) {
    instance->btn = (instance->data >> 13) & 0xFF;
    instance->serial = (instance->data >> 5) & 0xFF;
    instance->cnt = (instance->data >> 1) & 0xF;

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->btn);
    }
    subghz_custom_btn_set_max(2);

}

static const char* subghz_protocol_hay21_get_button_name(uint8_t btn) {
    const char* btn_name;
    switch(btn) {
    case 0x5A:
        btn_name = "On/Off";
        break;
    case 0xC3:
        btn_name = "Mode";
        break;
    case 0x88:
        btn_name = "Hold";
        break;
    default:
        btn_name = "Unknown";
        break;
    }
    return btn_name;
}

static void hay21_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderHay21* instance = input->decoder;


    subghz_protocol_hay21_remote_controller(&instance->generic);

    subghz_block_generic_global.cnt_is_available = true;
    subghz_block_generic_global.cnt_length_bit = 8;
    subghz_block_generic_global.current_cnt = instance->generic.cnt;

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 8;

    furi_string_cat_printf(
        output,
        "%s - %dbit\r\n"
        "Key:0x%06lX\r\n"
        "Serial:0x%02X\r\n"
        "Btn:0x%01X - %s\r\n"
        "Cnt:%01X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data & 0xFFFFFFFF),
        (uint8_t)(instance->generic.serial & 0xFF),
        instance->generic.btn,
        subghz_protocol_hay21_get_button_name(instance->generic.btn),
        (uint8_t)(instance->generic.cnt & 0xF));
}

static const SubGhzDecodeEntry hay21_items[] = {{.name = "Hay21", .get_string = hay21_decode}};
static const SubGhzDecodeRegistry hay21_registry = {.items = hay21_items, .size = 1};
static const FlipperAppPluginDescriptor decode_hay21_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &hay21_registry};
const FlipperAppPluginDescriptor* decode_hay21_ep(void) { return &decode_hay21_descriptor; }
