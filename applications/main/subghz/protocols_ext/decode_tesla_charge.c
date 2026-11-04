/* decode_tesla_charge.fal : SD get_string for tesla_charge (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/tesla_charge.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>

struct SubGhzProtocolDecoderTeslaCharge {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;
};

static void tesla_charge_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderTeslaCharge* instance = input->decoder;


    // push protocol data to global variable
    subghz_block_generic_global.btn_is_available = false;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%05lX%08lX\r\n"
        "Tesla Charge Port\r\n"
        "315(US)/433.92(EU) AM\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data >> 32) & 0xFFFFF,
        (uint32_t)(instance->generic.data & 0xFFFFFFFF));
}

static const SubGhzDecodeEntry tesla_charge_items[] = {{.name = "Tesla Charge", .get_string = tesla_charge_decode}};
static const SubGhzDecodeRegistry tesla_charge_registry = {.items = tesla_charge_items, .size = 1};
static const FlipperAppPluginDescriptor decode_tesla_charge_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &tesla_charge_registry};
const FlipperAppPluginDescriptor* decode_tesla_charge_ep(void) { return &decode_tesla_charge_descriptor; }
