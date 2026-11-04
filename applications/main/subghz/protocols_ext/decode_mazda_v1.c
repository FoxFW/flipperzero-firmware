/* decode_mazda_v1.fal : SD get_string for mazda_v1 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/blocks/bitbuf.h>
#include <lib/subghz/protocols/mazda_v1.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define MAZDA_V1_CAP        400

struct SubGhzProtocolDecoderMazdaV1 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t parser_step;
    bool half_bit;
    uint16_t all_count;
    int32_t last_pos;
    uint8_t all_bits[SUBGHZ_BITBUF_BYTES(MAZDA_V1_CAP)];

    uint8_t serial; // 6-bit
    uint8_t btn; // 8-bit
    uint8_t cnt; // 5-bit
    uint8_t tail; // frame bits 64..70 (7-bit remainder)
};

static const char* mazda_v1_button_name(uint8_t btn);

static const char* mazda_v1_button_name(uint8_t btn) {
    switch(btn & 0x7F) {
    case 0x06: return "Lock";
    case 0x0A: return "Unlock";
    case 0x0E: return "Trunk";
    case 0x26: return "Btn5/Trunk";
    default: return "?";
    }
}

static void mazda_v1_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderMazdaV1* instance = input->decoder;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Serial:%02X Btn:%02X %s\r\n"
        "Cnt:%02X (structural, cnt=low5)\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)(instance->generic.data >> 32),
        (unsigned long)(instance->generic.data & 0xFFFFFFFF),
        instance->serial,
        instance->btn,
        mazda_v1_button_name(instance->btn),
        instance->cnt);
}

static const SubGhzDecodeEntry mazda_v1_items[] = {{.name = "Mazda V1", .get_string = mazda_v1_decode}};
static const SubGhzDecodeRegistry mazda_v1_registry = {.items = mazda_v1_items, .size = 1};
static const FlipperAppPluginDescriptor decode_mazda_v1_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &mazda_v1_registry};
const FlipperAppPluginDescriptor* decode_mazda_v1_ep(void) { return &decode_mazda_v1_descriptor; }
