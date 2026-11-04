/* decode_audi_v2.fal : SD get_string for audi_v2 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/audi_v2.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>

struct SubGhzProtocolDecoderAudiV2 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;
    uint8_t parser_step;
    uint16_t preamble_count;
    uint64_t gdata; // first 64 bits
    uint64_t acc; // running accumulator
    uint16_t bit_count;
    uint8_t trailer;
    bool crc_ok;
};

static void audi_v2_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderAudiV2* instance = input->decoder;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Trailer:%02X %s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)(instance->generic.data >> 32),
        (unsigned long)(instance->generic.data & 0xFFFFFFFFU),
        instance->trailer,
        instance->crc_ok ? "OK" : "--");
}

static const SubGhzDecodeEntry audi_v2_items[] = {{.name = "Audi V2", .get_string = audi_v2_decode}};
static const SubGhzDecodeRegistry audi_v2_registry = {.items = audi_v2_items, .size = 1};
static const FlipperAppPluginDescriptor decode_audi_v2_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &audi_v2_registry};
const FlipperAppPluginDescriptor* decode_audi_v2_ep(void) { return &decode_audi_v2_descriptor; }
