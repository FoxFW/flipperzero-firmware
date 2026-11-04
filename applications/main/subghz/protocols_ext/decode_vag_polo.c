/* decode_vag_polo.fal : SD get_string for vag_polo (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/vag_polo.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>

#define VAG_POLO_BYTES       10

struct SubGhzProtocolDecoderVagPolo {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;
    uint8_t parser_step;
    uint16_t sync_reg;
    uint16_t pre_ones;
    uint8_t payload[VAG_POLO_BYTES];
    uint16_t pbits;

    uint32_t serial;
    uint8_t btn; // 1 = Unlock, 2 = Lock
    uint8_t button_raw; // raw payload[0]
    uint16_t hdr16; // top 16 bits of the 80-bit frame (payload[0..1])
};

static const char* vag_polo_button_name(uint8_t hi_nibble);

static const char* vag_polo_button_name(uint8_t hi_nibble) {
    switch(hi_nibble) {
    case 0x7: return "Unlock";
    case 0xB: return "Lock";
    default: return "?";
    }
}

static void vag_polo_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderVagPolo* instance = input->decoder;

    uint8_t hi = (instance->button_raw >> 4) & 0x0F;
    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Code:%08lX%08lX\r\n"
        "Serial:%08lX\r\n"
        "Btn:%01X %s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)(instance->generic.data >> 32),
        (unsigned long)(instance->generic.data & 0xFFFFFFFFU),
        (unsigned long)instance->serial,
        hi,
        vag_polo_button_name(hi));
}

static const SubGhzDecodeEntry vag_polo_items[] = {{.name = "VAG Polo", .get_string = vag_polo_decode}};
static const SubGhzDecodeRegistry vag_polo_registry = {.items = vag_polo_items, .size = 1};
static const FlipperAppPluginDescriptor decode_vag_polo_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &vag_polo_registry};
const FlipperAppPluginDescriptor* decode_vag_polo_ep(void) { return &decode_vag_polo_descriptor; }
