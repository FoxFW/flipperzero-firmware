/* decode_legrand.fal : SD get_string for legrand (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/legrand.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderLegrand {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t te;
    uint32_t last_data;
};

static void legrand_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderLegrand* instance = input->decoder;


    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%05lX\r\n"
        "Te:%luus\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data & 0xFFFFFF),
        instance->te);
}

static const SubGhzDecodeEntry legrand_items[] = {{.name = "Legrand", .get_string = legrand_decode}};
static const SubGhzDecodeRegistry legrand_registry = {.items = legrand_items, .size = 1};
static const FlipperAppPluginDescriptor decode_legrand_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &legrand_registry};
const FlipperAppPluginDescriptor* decode_legrand_ep(void) { return &decode_legrand_descriptor; }
