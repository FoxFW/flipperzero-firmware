/* decode_telcoma_edge.fal : SD get_string for telcoma_edge (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/telcoma_edge.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderTelcomaEdge {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    bool half_pending;
    bool half_level;
};

static void telcoma_edge_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderTelcomaEdge* instance = input->decoder;

    uint32_t data = (uint32_t)(instance->generic.data & 0xFFFFFFFF);
    uint32_t payload = data & 0xFFFFFF;

    uint32_t serial = (payload >> 3) & 0x1FFFFF;
    uint8_t channel = payload & 0x07;
    furi_string_cat_printf(
        output,
        "Telcoma/Cardin\nEDGE %db\r\n"
        "Key:0x%08lX\r\n"
        "Serial:0x%05lX\r\n"
        "Ch:0x%01X\r\n",
        instance->generic.data_count_bit,
        (unsigned long)data,
        (unsigned long)serial,
        channel);
}

static const SubGhzDecodeEntry telcoma_edge_items[] = {{.name = "Telcoma/Cardin EDGE", .get_string = telcoma_edge_decode}};
static const SubGhzDecodeRegistry telcoma_edge_registry = {.items = telcoma_edge_items, .size = 1};
static const FlipperAppPluginDescriptor decode_telcoma_edge_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &telcoma_edge_registry};
const FlipperAppPluginDescriptor* decode_telcoma_edge_ep(void) { return &decode_telcoma_edge_descriptor; }
