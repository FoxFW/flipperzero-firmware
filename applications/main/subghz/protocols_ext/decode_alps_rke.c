/* decode_alps_rke.fal : SD get_string for alps_rke (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/alps_rke.h>
#include <furi.h>
#include <string.h>
#include <lib/subghz/types.h>
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>
#include <flipper_format/flipper_format.h>

#define ALPS_RKE_MAX_BITS     96 /* decoded-bit ring capacity */

struct SubGhzProtocolDecoderAlpsRke {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t preamble_count;
    uint16_t bit_count;
    ManchesterState manchester_state;
    uint8_t bits[ALPS_RKE_MAX_BITS];
};

static void alps_rke_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderAlpsRke* instance = input->decoder;


    /* Re-derive fields from data so display is correct after load. */
    uint64_t data = instance->generic.data;
    uint32_t id = (uint32_t)(data >> 32);
    uint8_t button = (uint8_t)((data >> 28) & 0x0F);
    uint16_t sequence = (uint16_t)(data & 0xFFFF);

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Id:%08lX Btn:%X\r\n"
        "Cnt:%04X\r\n"
        "Key:%016llX",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)id,
        button,
        sequence,
        (unsigned long long)data);
}

static const SubGhzDecodeEntry alps_rke_items[] = {{.name = "Alps FWB1U545", .get_string = alps_rke_decode}};
static const SubGhzDecodeRegistry alps_rke_registry = {.items = alps_rke_items, .size = 1};
static const FlipperAppPluginDescriptor decode_alps_rke_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &alps_rke_registry};
const FlipperAppPluginDescriptor* decode_alps_rke_ep(void) { return &decode_alps_rke_descriptor; }
