/* decode_megacode.fal : SD get_string for megacode (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/megacode.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderMegaCode {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint8_t last_bit;
};

static void subghz_protocol_megacode_check_remote_controller(SubGhzBlockGeneric* instance);

static void subghz_protocol_megacode_check_remote_controller(SubGhzBlockGeneric* instance) {

    if((instance->data >> 23) == 1) {
        instance->serial = (instance->data >> 3) & 0xFFFF;
        instance->btn = instance->data & 0b111;
        instance->cnt = (instance->data >> 19) & 0b1111;
    } else {
        instance->serial = 0;
        instance->btn = 0;
        instance->cnt = 0;
    }
}

static void megacode_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderMegaCode* instance = input->decoder;

    subghz_protocol_megacode_check_remote_controller(&instance->generic);

    subghz_block_generic_global.btn_is_available = false;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 3;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%06lX\r\n"
        "Sn:0x%04lX - %lu\r\n"
        "Facility:%lX Btn:%X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)instance->generic.data,
        instance->generic.serial,
        instance->generic.serial,
        instance->generic.cnt,
        instance->generic.btn);
}

static const SubGhzDecodeEntry megacode_items[] = {{.name = "MegaCode", .get_string = megacode_decode}};
static const SubGhzDecodeRegistry megacode_registry = {.items = megacode_items, .size = 1};
static const FlipperAppPluginDescriptor decode_megacode_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &megacode_registry};
const FlipperAppPluginDescriptor* decode_megacode_ep(void) { return &decode_megacode_descriptor; }
