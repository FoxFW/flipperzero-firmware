/* decode_allstar_firefly.fal : SD get_string for allstar_firefly (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/allstar_firefly.h>
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
    ((((dip >> 0x10) & 0x3) == check_dip) ? '*' : '_'),    \
        ((((dip >> 0xE) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0xC) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0xA) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x8) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x6) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x4) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x2) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x0) & 0x3) == check_dip) ? '*' : '_')

struct SubGhzProtocolDecoderAllstarFirefly {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static void allstar_firefly_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderAllstarFirefly* instance = input->decoder;


    uint64_t code_found_reverse = subghz_protocol_blocks_reverse_key(
        instance->generic.data, instance->generic.data_count_bit);

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "Key:0x%05lX Yek:0x%05lX\r\n"
        "  +:   " DIP_PATTERN "\r\n"
        "  o:   " DIP_PATTERN "\r\n"
        "  -:   " DIP_PATTERN "\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)(instance->generic.data & 0xFFFFF),
        (unsigned long)(code_found_reverse & 0xFFFFF),
        SHOW_DIP_P(instance->generic.data, DIP_P),
        SHOW_DIP_P(instance->generic.data, DIP_O),
        SHOW_DIP_P(instance->generic.data, DIP_N));
}

static const SubGhzDecodeEntry allstar_firefly_items[] = {{.name = "Allstar Firefly", .get_string = allstar_firefly_decode}};
static const SubGhzDecodeRegistry allstar_firefly_registry = {.items = allstar_firefly_items, .size = 1};
static const FlipperAppPluginDescriptor decode_allstar_firefly_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &allstar_firefly_registry};
const FlipperAppPluginDescriptor* decode_allstar_firefly_ep(void) { return &decode_allstar_firefly_descriptor; }
