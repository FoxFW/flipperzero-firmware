/* decode_chevrolet_v0.fal : SD get_string for chevrolet_v0 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/chevrolet_v0.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>

struct SubGhzProtocolDecoderChevroletV0 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;
    uint32_t data_header; // 32-bit preamble / high shift register
    uint16_t bit_count; // preamble bit counter
    uint16_t data_count; // collected data bits

    uint32_t serial; // 24-bit
    uint8_t btn; // 4-bit
    uint64_t hop; // 36-bit
    uint32_t seed; // low 32 bits of payload
};

static const char* chevrolet_v0_button_name(uint8_t btn);

static const char* chevrolet_v0_button_name(uint8_t btn) {
    switch(btn) {
    case 0xA: return "Open";
    case 0xB: return "Close";
    default: return "?";
    }
}

static void chevrolet_v0_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderChevroletV0* instance = input->decoder;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Serial:%06lX Btn:%X %s\r\n"
        "Hop:%03lX%08lX Seed:%08lX\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)(instance->generic.data >> 32),
        (unsigned long)(instance->generic.data & 0xFFFFFFFF),
        (unsigned long)instance->serial,
        instance->btn,
        chevrolet_v0_button_name(instance->btn),
        (unsigned long)(instance->hop >> 32),
        (unsigned long)(instance->hop & 0xFFFFFFFF),
        (unsigned long)instance->seed);
}

static const SubGhzDecodeEntry chevrolet_v0_items[] = {{.name = "Chevrolet V0", .get_string = chevrolet_v0_decode}};
static const SubGhzDecodeRegistry chevrolet_v0_registry = {.items = chevrolet_v0_items, .size = 1};
static const FlipperAppPluginDescriptor decode_chevrolet_v0_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &chevrolet_v0_registry};
const FlipperAppPluginDescriptor* decode_chevrolet_v0_ep(void) { return &decode_chevrolet_v0_descriptor; }
