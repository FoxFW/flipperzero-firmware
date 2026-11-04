/* decode_star_line.fal : SD get_string for star_line (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/star_line.h>
#include <lib/subghz/protocols/keeloq_common.h>
#include <lib/subghz/subghz_keystore.h>
#include <lib/subghz/subghz_keystore_i.h>
#include <lib/subghz/blocks/custom_btn_i.h>

struct SubGhzProtocolDecoderStarLine {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t header_count;
    SubGhzKeystore* keystore;
    const char* manufacture_name;

    FuriString* manufacture_from_file;
};

static const char* star_line_btn_name(uint8_t btn);
static uint8_t star_line_btn_to_custom(uint8_t btn);
static uint8_t star_line_custom_to_btn(uint8_t custom, uint8_t original_btn);

static const char* star_line_btn_name(uint8_t btn) {
    switch(btn) {
    case 0x01: return "Lock";
    case 0x02: return "Unlock";
    case 0x03: return "Trunk";
    case 0x04: return "Panic";
    case 0x21: return "Lock";
    case 0x22: return "Unlock";
    case 0x23: return "Trunk";
    case 0x24: return "Start";
    case 0x25: return "Stop";
    case 0x26: return "Extra";
    default:   return "Unknown";
    }
}

static uint8_t star_line_btn_to_custom(uint8_t btn) {
    switch(btn) {
    case 0x01:
    case 0x21: return SUBGHZ_CUSTOM_BTN_UP;
    case 0x02:
    case 0x22: return SUBGHZ_CUSTOM_BTN_DOWN;
    case 0x03:
    case 0x23: return SUBGHZ_CUSTOM_BTN_LEFT;
    case 0x04:
    case 0x24: return SUBGHZ_CUSTOM_BTN_RIGHT;
    default:   return SUBGHZ_CUSTOM_BTN_OK;
    }
}

static uint8_t star_line_custom_to_btn(uint8_t custom, uint8_t original_btn) {
    if(custom == SUBGHZ_CUSTOM_BTN_OK) return original_btn;

    bool is_twage = (original_btn & 0x20) != 0;

    switch(custom) {
    case SUBGHZ_CUSTOM_BTN_UP:    return is_twage ? 0x21 : 0x01;
    case SUBGHZ_CUSTOM_BTN_DOWN:  return is_twage ? 0x22 : 0x02;
    case SUBGHZ_CUSTOM_BTN_LEFT:  return is_twage ? 0x23 : 0x03;
    case SUBGHZ_CUSTOM_BTN_RIGHT: return is_twage ? 0x24 : 0x04;
    default: return original_btn;
    }
}

static void star_line_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderStarLine* instance = input->decoder;


    /* key derivation done internally */

    subghz_custom_btn_set_original(star_line_btn_to_custom(instance->generic.btn));
    subghz_custom_btn_set_max(4);
    subghz_custom_btn_set_pages(true);
        subghz_custom_btn_set_max_pages(2);

    uint32_t code_found_hi = instance->generic.data >> 32;
    uint32_t code_found_lo = instance->generic.data & 0x00000000ffffffff;

    uint64_t code_found_reverse = subghz_protocol_blocks_reverse_key(
        instance->generic.data, instance->generic.data_count_bit);
    uint32_t code_found_reverse_hi = code_found_reverse >> 32;
    uint32_t code_found_reverse_lo = code_found_reverse & 0x00000000ffffffff;

    uint8_t display_btn;
    uint8_t custom = subghz_custom_btn_get();
    if(custom == SUBGHZ_CUSTOM_BTN_OK) {
        display_btn = instance->generic.btn;
    } else {
        display_btn = star_line_custom_to_btn(custom, instance->generic.btn);
    }

    bool is_twage = (instance->generic.btn & 0x20) != 0;

    if(is_twage) {
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key:%08lX%08lX\r\n"
            "Fix:0x%08lX\r\n"
            "Hop:0x%08lX\r\n"
            "Btn:[%s] Cnt:%04lX\r\n",
            instance->generic.protocol_name,
            instance->generic.data_count_bit,
            code_found_hi,
            code_found_lo,
            code_found_reverse_hi,
            code_found_reverse_lo,
            star_line_btn_name(display_btn),
            instance->generic.cnt);
    } else {

        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key:%08lX%08lX\r\n"
            "Fix:0x%08lX\r\n"
            "Hop:0x%08lX\r\n"
            "Btn:[%s] Cnt:%04lX\r\n",
            instance->generic.protocol_name,
            instance->generic.data_count_bit,
            code_found_hi,
            code_found_lo,
            code_found_reverse_hi,
            code_found_reverse_lo,
            star_line_btn_name(display_btn),
            instance->generic.cnt);
    }
}

static const SubGhzDecodeEntry star_line_items[] = {{.name = "Star Line", .get_string = star_line_decode}};
static const SubGhzDecodeRegistry star_line_registry = {.items = star_line_items, .size = 1};
static const FlipperAppPluginDescriptor decode_star_line_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &star_line_registry};
const FlipperAppPluginDescriptor* decode_star_line_ep(void) { return &decode_star_line_descriptor; }
