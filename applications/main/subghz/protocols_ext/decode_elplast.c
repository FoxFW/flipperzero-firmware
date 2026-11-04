/* decode_elplast.fal : SD get_string for elplast (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/elplast.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderElplast {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static void elplast_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderElplast* instance = input->decoder;


    uint64_t code_found_reverse = subghz_protocol_blocks_reverse_key(
        instance->generic.data, instance->generic.data_count_bit);

    uint32_t code_found_reverse_lo = code_found_reverse & 0x000003ffffffffff;

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "Key: 0x%05lX\r\n"
        "Yek: 0x%05lX",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data & 0xFFFFFF),
        code_found_reverse_lo);
}

static const SubGhzDecodeEntry elplast_items[] = {{.name = "Elplast", .get_string = elplast_decode}};
static const SubGhzDecodeRegistry elplast_registry = {.items = elplast_items, .size = 1};
static const FlipperAppPluginDescriptor decode_elplast_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &elplast_registry};
const FlipperAppPluginDescriptor* decode_elplast_ep(void) { return &decode_elplast_descriptor; }
