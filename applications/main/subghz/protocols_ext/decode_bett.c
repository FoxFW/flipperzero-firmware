/* decode_bett.fal : SD get_string for bett (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/bett.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define DIP_P 0b11
#define DIP_O 0b10
#define DIP_N 0b00
#define DIP_PATTERN "%c%c%c%c%c%c%c%c%c"
#define SHOW_DIP_P(dip, check_dip)                         \
    ((((dip >> 0x8) >> 0x8) == check_dip) ? '*' : '_'),    \
        ((((dip >> 0xE) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0xC) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0xA) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x8) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x6) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x4) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x2) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x0) & 0x3) == check_dip) ? '*' : '_')

struct SubGhzProtocolDecoderBETT {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static void bett_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderBETT* instance = input->decoder;

    uint32_t data = (uint32_t)(instance->generic.data & 0x3FFFF);
    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%05lX\r\n"
        "  +:   " DIP_PATTERN "\r\n"
        "  o:   " DIP_PATTERN "\r\n"
        "  -:   " DIP_PATTERN "\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        data,
        SHOW_DIP_P(data, DIP_P),
        SHOW_DIP_P(data, DIP_O),
        SHOW_DIP_P(data, DIP_N));
}

static const SubGhzDecodeEntry bett_items[] = {{.name = "BETT", .get_string = bett_decode}};
static const SubGhzDecodeRegistry bett_registry = {.items = bett_items, .size = 1};
static const FlipperAppPluginDescriptor decode_bett_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &bett_registry};
const FlipperAppPluginDescriptor* decode_bett_ep(void) { return &decode_bett_descriptor; }
