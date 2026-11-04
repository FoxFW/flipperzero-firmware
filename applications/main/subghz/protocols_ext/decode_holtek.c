/* decode_holtek.fal : SD get_string for holtek (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/holtek.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define HOLTEK_HEADER_MASK 0xF000000000
#define HOLTEK_HEADER      0x5000000000

struct SubGhzProtocolDecoderHoltek {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static void subghz_protocol_holtek_check_remote_controller(SubGhzBlockGeneric* instance);

static void subghz_protocol_holtek_check_remote_controller(SubGhzBlockGeneric* instance) {
    if((instance->data & HOLTEK_HEADER_MASK) == HOLTEK_HEADER) {
        instance->serial =
            subghz_protocol_blocks_reverse_key((instance->data >> 16) & 0xFFFFF, 20);
        uint16_t btn = instance->data & 0xFFFF;
        if((btn & 0xf) != 0xA) {
            instance->btn = 0x1 << 4 | (btn & 0xF);
        } else if(((btn >> 4) & 0xF) != 0xA) {
            instance->btn = 0x2 << 4 | ((btn >> 4) & 0xF);
        } else if(((btn >> 8) & 0xF) != 0xA) {
            instance->btn = 0x3 << 4 | ((btn >> 8) & 0xF);
        } else if(((btn >> 12) & 0xF) != 0xA) {
            instance->btn = 0x4 << 4 | ((btn >> 12) & 0xF);
        } else {
            instance->btn = 0;
        }
    } else {
        instance->serial = 0;
        instance->btn = 0;
        instance->cnt = 0;
    }
}

static void holtek_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderHoltek* instance = input->decoder;

    subghz_protocol_holtek_check_remote_controller(&instance->generic);

    subghz_block_generic_global.btn_is_available = false;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 4;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%lX%08lX\r\n"
        "Sn:0x%05lX Btn:%X ",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)((instance->generic.data >> 32) & 0xFFFFFFFF),
        (uint32_t)(instance->generic.data & 0xFFFFFFFF),
        instance->generic.serial,
        instance->generic.btn >> 4);

    if((instance->generic.btn & 0xF) == 0xE) {
        furi_string_cat_printf(output, "ON\r\n");
    } else if((instance->generic.btn & 0xF) == 0xB) {
        furi_string_cat_printf(output, "OFF\r\n");
    }
}

static const SubGhzDecodeEntry holtek_items[] = {{.name = "Holtek", .get_string = holtek_decode}};
static const SubGhzDecodeRegistry holtek_registry = {.items = holtek_items, .size = 1};
static const FlipperAppPluginDescriptor decode_holtek_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &holtek_registry};
const FlipperAppPluginDescriptor* decode_holtek_ep(void) { return &decode_holtek_descriptor; }
