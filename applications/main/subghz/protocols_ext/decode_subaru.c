/* decode_subaru.fal : SD get_string for subaru (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/subaru.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>

struct SubGhzProtocolDecoderSubaru {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    
    uint16_t header_count;
    uint16_t bit_count;
    uint8_t data[8];
    
    uint64_t key;
    uint32_t serial;
    uint8_t button;
    uint16_t count;
};

static uint8_t subaru_get_button_code(uint8_t custom_btn);
static const char* subaru_get_button_name(uint8_t btn);

static uint8_t subaru_get_button_code(uint8_t custom_btn) {
    switch(custom_btn) {
        case 1: return 0x01;
        case 2: return 0x02;
        case 3: return 0x03;
        case 4: return 0x04;
        case 5: return 0x08;
        default: return 0x01;
    }
}

static const char* subaru_get_button_name(uint8_t btn) {
    switch(btn) {
        case 0x01: return "Lock";
        case 0x02: return "Unlock";
        case 0x03: return "Trunk";
        case 0x04: return "Panic";
        case 0x08: return "0x08";
        default: return "??";
    }
}

static void subaru_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderSubaru* instance = input->decoder;

    
    uint32_t key_hi = (uint32_t)(instance->key >> 32);
    uint32_t key_lo = (uint32_t)(instance->key & 0xFFFFFFFF);

    subghz_custom_btn_set_max(5);
    uint8_t selected_custom_btn = (subghz_custom_btn_get() == SUBGHZ_CUSTOM_BTN_OK) ?
                                       subghz_custom_btn_get_original() :
                                       subghz_custom_btn_get();
    uint8_t display_btn = subaru_get_button_code(selected_custom_btn);
    
    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Sn:%06lX Cnt:%04X\r\n"
        "Btn:%X [%s]",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        key_hi,
        key_lo,
        instance->serial,
        instance->count,
        display_btn,
        subaru_get_button_name(display_btn));
}

static const SubGhzDecodeEntry subaru_items[] = {{.name = "SUBARU", .get_string = subaru_decode}};
static const SubGhzDecodeRegistry subaru_registry = {.items = subaru_items, .size = 1};
static const FlipperAppPluginDescriptor decode_subaru_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &subaru_registry};
const FlipperAppPluginDescriptor* decode_subaru_ep(void) { return &decode_subaru_descriptor; }
