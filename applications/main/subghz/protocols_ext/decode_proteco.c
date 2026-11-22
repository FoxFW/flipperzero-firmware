
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/proteco.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderProteco {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint32_t te;
    uint32_t guard;
};

static void proteco_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderProteco* instance = input->decoder;
    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%06lX\r\n"
        "Sn:0x%06lX  Btn:%X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data & 0xFFFFFF),
        instance->generic.serial,
        instance->generic.btn);
}

static const SubGhzDecodeEntry proteco_items[] = {{.name = "Proteco", .get_string = proteco_decode}};
static const SubGhzDecodeRegistry proteco_registry = {.items = proteco_items, .size = 1};
static const FlipperAppPluginDescriptor decode_proteco_descriptor = {
    .appid = SUBGHZ_DECODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION,
    .entry_point = &proteco_registry};
const FlipperAppPluginDescriptor* decode_proteco_ep(void) {
    return &decode_proteco_descriptor;
}
