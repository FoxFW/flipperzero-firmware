/* decode_mazda_v2.fal : SD get_string for mazda_v2 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/blocks/bitbuf.h>
#include <lib/subghz/protocols/mazda_v2.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define MAZDA_V2_CAP         400

struct SubGhzProtocolDecoderMazdaV2 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t parser_step;
    bool half_bit;
    uint16_t all_count;
    uint8_t all_bits[SUBGHZ_BITBUF_BYTES(MAZDA_V2_CAP)];

    uint8_t serial;
    uint8_t btn;
    uint8_t cnt;
};

static const char* mazda_v2_button_name(uint8_t btn);

static const char* mazda_v2_button_name(uint8_t btn) {
    switch(btn & 0x7F) {
    case 0x06: return "Lock";
    case 0x0A: return "Unlock";
    case 0x0E: return "Trunk";
    default: return "?";
    }
}

static void mazda_v2_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderMazdaV2* instance = input->decoder;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Serial:%02X Btn:%02X %s\r\n"
        "Cnt:%02X (structural)\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)(instance->generic.data >> 32),
        (unsigned long)(instance->generic.data & 0xFFFFFFFF),
        instance->serial,
        instance->btn,
        mazda_v2_button_name(instance->btn),
        instance->cnt);
}

static const SubGhzDecodeEntry mazda_v2_items[] = {{.name = "Mazda V2", .get_string = mazda_v2_decode}};
static const SubGhzDecodeRegistry mazda_v2_registry = {.items = mazda_v2_items, .size = 1};
static const FlipperAppPluginDescriptor decode_mazda_v2_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &mazda_v2_registry};
const FlipperAppPluginDescriptor* decode_mazda_v2_ep(void) { return &decode_mazda_v2_descriptor; }
