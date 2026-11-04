/* decode_bmw_v2.fal : SD get_string for bmw_v2 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/bmw_v2.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderBmwV2 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder; // parser_step / te_last / decode_data / decode_count_bit
    SubGhzBlockGeneric generic;

    uint8_t header;
    uint8_t trailer;
    uint32_t serial; // 24-bit, carved from the encrypted rolling code
    uint16_t cnt; // 16-bit, low part of the rolling code
};

static void bmw_v2_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderBmwV2* instance = input->decoder;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Hdr:%02X Trl:%02X\r\n"
        "Sn:%06lX Cnt:%04X\r\n"
        "(rolling serial - not stable)\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)(instance->generic.data >> 32),
        (unsigned long)(instance->generic.data & 0xFFFFFFFFU),
        instance->header,
        instance->trailer,
        (unsigned long)instance->serial,
        instance->cnt);
}

static const SubGhzDecodeEntry bmw_v2_items[] = {{.name = "BMW V2", .get_string = bmw_v2_decode}};
static const SubGhzDecodeRegistry bmw_v2_registry = {.items = bmw_v2_items, .size = 1};
static const FlipperAppPluginDescriptor decode_bmw_v2_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &bmw_v2_registry};
const FlipperAppPluginDescriptor* decode_bmw_v2_ep(void) { return &decode_bmw_v2_descriptor; }
