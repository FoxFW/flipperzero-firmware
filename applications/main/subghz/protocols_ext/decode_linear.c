/* decode_linear.fal : SD get_string for linear (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/linear.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define DIP_PATTERN "%c%c%c%c%c%c%c%c%c%c"
#define DATA_TO_DIP(dip)                                                                    \
    (dip & 0x0200 ? '1' : '0'), (dip & 0x0100 ? '1' : '0'), (dip & 0x0080 ? '1' : '0'),     \
        (dip & 0x0040 ? '1' : '0'), (dip & 0x0020 ? '1' : '0'), (dip & 0x0010 ? '1' : '0'), \
        (dip & 0x0008 ? '1' : '0'), (dip & 0x0004 ? '1' : '0'), (dip & 0x0002 ? '1' : '0'), \
        (dip & 0x0001 ? '1' : '0')

struct SubGhzProtocolDecoderLinear {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static void linear_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderLinear* instance = input->decoder;


    uint32_t code_found_lo = ~instance->generic.data & 0x00000000000003ff;

    uint64_t code_found_reverse = subghz_protocol_blocks_reverse_key(
        ~instance->generic.data, instance->generic.data_count_bit);

    uint32_t code_found_reverse_lo = code_found_reverse & 0x00000000000003ff;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%03lX\r\n"
        "Yek:0x%03lX\r\n"
        "DIP:" DIP_PATTERN "\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        code_found_lo,
        code_found_reverse_lo,
        DATA_TO_DIP(code_found_lo));
}

static const SubGhzDecodeEntry linear_items[] = {{.name = "Linear", .get_string = linear_decode}};
static const SubGhzDecodeRegistry linear_registry = {.items = linear_items, .size = 1};
static const FlipperAppPluginDescriptor decode_linear_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &linear_registry};
const FlipperAppPluginDescriptor* decode_linear_ep(void) { return &decode_linear_descriptor; }
