/* decode_bmw_cas4.fal : SD get_string for bmw_cas4 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/bmw_cas4.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>

struct SubGhzProtocolDecoderBmwCas4 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    ManchesterState manchester_state;
    uint8_t decoder_state;
    uint16_t preamble_count;
    uint8_t raw_data[8];
    uint8_t bit_count;
    uint32_t te_last;
};

static void bmw_cas4_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderBmwCas4* instance = input->decoder;


    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Raw:%02X %02X%02X%02X%02X%02X %02X %02X\r\n",
        instance->generic.protocol_name,
        (int)instance->generic.data_count_bit,
        instance->raw_data[0],
        instance->raw_data[1], instance->raw_data[2],
        instance->raw_data[3], instance->raw_data[4], instance->raw_data[5],
        instance->raw_data[6],
        instance->raw_data[7]);
}

static const SubGhzDecodeEntry bmw_cas4_items[] = {{.name = "BMW CAS4", .get_string = bmw_cas4_decode}};
static const SubGhzDecodeRegistry bmw_cas4_registry = {.items = bmw_cas4_items, .size = 1};
static const FlipperAppPluginDescriptor decode_bmw_cas4_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &bmw_cas4_registry};
const FlipperAppPluginDescriptor* decode_bmw_cas4_ep(void) { return &decode_bmw_cas4_descriptor; }
