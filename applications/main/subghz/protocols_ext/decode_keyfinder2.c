/* decode_keyfinder2.fal : SD get_string for keyfinder2 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/keyfinder2.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderKeyFinder2 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static const char* subghz_protocol_keyfinder2_color(uint8_t btn);
static void subghz_protocol_keyfinder2_check_remote_controller(SubGhzBlockGeneric* instance);

static const char* subghz_protocol_keyfinder2_color(uint8_t btn) {
    switch(btn) {
    case 3:
        return "Purple";
    case 4:
        return "Blue";
    case 5:
        return "Green";
    case 6:
        return "Red";
    default:
        return "Unknown";
    }
}

static void subghz_protocol_keyfinder2_check_remote_controller(SubGhzBlockGeneric* instance) {
    instance->btn = instance->data & 0x7;
}

static void keyfinder2_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderKeyFinder2* instance = input->decoder;


    subghz_protocol_keyfinder2_check_remote_controller(&instance->generic);

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "Key: 0x%03lX\r\n"
        "Btn: %01X  %s",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data & 0x7FF),
        instance->generic.btn,
        subghz_protocol_keyfinder2_color(instance->generic.btn));
}

static const SubGhzDecodeEntry keyfinder2_items[] = {{.name = "KeyFinder2", .get_string = keyfinder2_decode}};
static const SubGhzDecodeRegistry keyfinder2_registry = {.items = keyfinder2_items, .size = 1};
static const FlipperAppPluginDescriptor decode_keyfinder2_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &keyfinder2_registry};
const FlipperAppPluginDescriptor* decode_keyfinder2_ep(void) { return &decode_keyfinder2_descriptor; }
