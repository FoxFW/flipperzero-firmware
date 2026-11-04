/* decode_keyfinder.fal : SD get_string for keyfinder (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/keyfinder.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderKeyFinder {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint8_t end_count;
};

static void subghz_protocol_keyfinder_check_remote_controller(SubGhzBlockGeneric* instance);

static void subghz_protocol_keyfinder_check_remote_controller(SubGhzBlockGeneric* instance) {
    instance->serial = instance->data >> 4;
    instance->btn = instance->data & 0xF;
}

static void keyfinder_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderKeyFinder* instance = input->decoder;


    subghz_protocol_keyfinder_check_remote_controller(&instance->generic);

    uint64_t code_found_reverse = subghz_protocol_blocks_reverse_key(
        instance->generic.data, instance->generic.data_count_bit);

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "Key: 0x%06lX\r\n"
        "Yek: 0x%06lX\r\n"
        "Serial: 0x%05lX\r\n"
        "ID: 0x%0X",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data & 0xFFFFFF),
        (uint32_t)(code_found_reverse & 0xFFFFFF),
        instance->generic.serial,
        instance->generic.btn);
}

static const SubGhzDecodeEntry keyfinder_items[] = {{.name = "KeyFinder", .get_string = keyfinder_decode}};
static const SubGhzDecodeRegistry keyfinder_registry = {.items = keyfinder_items, .size = 1};
static const FlipperAppPluginDescriptor decode_keyfinder_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &keyfinder_registry};
const FlipperAppPluginDescriptor* decode_keyfinder_ep(void) { return &decode_keyfinder_descriptor; }
