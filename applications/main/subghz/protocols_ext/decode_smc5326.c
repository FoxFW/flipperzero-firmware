/* decode_smc5326.fal : SD get_string for smc5326 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/smc5326.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define DIP_P 0b11
#define DIP_O 0b10
#define DIP_N 0b00
#define DIP_PATTERN "%c%c%c%c%c%c%c%c"
#define SHOW_DIP_P(dip, check_dip)                         \
    ((((dip >> 0xE) & 0x3) == check_dip) ? '*' : '_'),     \
        ((((dip >> 0xC) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0xA) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x8) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x6) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x4) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x2) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x0) & 0x3) == check_dip) ? '*' : '_')

struct SubGhzProtocolDecoderSMC5326 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t te;
    uint32_t last_data;
};

static void subghz_protocol_smc5326_get_event_serialize(uint8_t event, FuriString* output);

static void subghz_protocol_smc5326_get_event_serialize(uint8_t event, FuriString* output) {
    furi_string_cat_printf(
        output,
        "%s%s%s%s\r\n",
        (((event >> 6) & 0x3) == 0x3 ? "B1 " : ""),
        (((event >> 4) & 0x3) == 0x3 ? "B2 " : ""),
        (((event >> 2) & 0x3) == 0x3 ? "B3 " : ""),
        (((event >> 0) & 0x3) == 0x3 ? "B4 " : ""));
}

static void smc5326_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderSMC5326* instance = input->decoder;

    uint32_t data = (uint32_t)((instance->generic.data >> 9) & 0xFFFF);

    furi_string_cat_printf(
        output,
        "%s %ubit\r\n"
        "Key:%07lX         Te:%luus\r\n"
        "  +:   " DIP_PATTERN "\r\n"
        "  o:   " DIP_PATTERN "    ",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data & 0x1FFFFFF),
        instance->te,
        SHOW_DIP_P(data, DIP_P),
        SHOW_DIP_P(data, DIP_O));
    subghz_protocol_smc5326_get_event_serialize(instance->generic.data >> 1, output);
    furi_string_cat_printf(output, "  -:   " DIP_PATTERN "\r\n", SHOW_DIP_P(data, DIP_N));
}

static const SubGhzDecodeEntry smc5326_items[] = {{.name = "SMC5326", .get_string = smc5326_decode}};
static const SubGhzDecodeRegistry smc5326_registry = {.items = smc5326_items, .size = 1};
static const FlipperAppPluginDescriptor decode_smc5326_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &smc5326_registry};
const FlipperAppPluginDescriptor* decode_smc5326_ep(void) { return &decode_smc5326_descriptor; }
