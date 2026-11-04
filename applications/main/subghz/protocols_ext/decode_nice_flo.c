/* decode_nice_flo.fal : SD get_string for nice_flo (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/nice_flo.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderNiceFlo {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static void nice_flo_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderNiceFlo* instance = input->decoder;


    uint32_t code_found_lo = instance->generic.data & 0x00000000ffffffff;
    uint64_t code_found_reverse = subghz_protocol_blocks_reverse_key(
        instance->generic.data, instance->generic.data_count_bit);
    uint32_t code_found_reverse_lo = code_found_reverse & 0x00000000ffffffff;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%08lX\r\n"
        "Yek:0x%08lX\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        code_found_lo,
        code_found_reverse_lo);
}

static const SubGhzDecodeEntry nice_flo_items[] = {{.name = "Nice FLO", .get_string = nice_flo_decode}};
static const SubGhzDecodeRegistry nice_flo_registry = {.items = nice_flo_items, .size = 1};
static const FlipperAppPluginDescriptor decode_nice_flo_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &nice_flo_registry};
const FlipperAppPluginDescriptor* decode_nice_flo_ep(void) { return &decode_nice_flo_descriptor; }
