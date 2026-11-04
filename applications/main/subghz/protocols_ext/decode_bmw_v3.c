/* decode_bmw_v3.fal : SD get_string for bmw_v3 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/bmw_v3.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderBmwV3 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t step; // 0 reset, 1 waitgap, 2 findheader, 3 collectdata
    uint64_t data;
    uint16_t nbit;
    uint32_t te_last;

    uint16_t serial; // 16-bit cleartext serial
    uint8_t btn;
    uint16_t cnt; // 16-bit cleartext counter field
};

static const char* bmw_v3_button_name(uint8_t btn);

static const char* bmw_v3_button_name(uint8_t btn) {
    switch(btn) {
    case 0x02: return "Trunk";
    case 0x04: return "Unlock";
    case 0x08: return "Lock";
    case 0x10: return "0x10";
    default: return "?";
    }
}

static void bmw_v3_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderBmwV3* instance = input->decoder;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Serial:%04X Btn:%02X %s\r\n"
        "Cnt:%04X (RC=AES)\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)(instance->generic.data >> 32),
        (unsigned long)(instance->generic.data & 0xFFFFFFFFU),
        instance->serial,
        instance->btn,
        bmw_v3_button_name(instance->btn),
        instance->cnt);
}

static const SubGhzDecodeEntry bmw_v3_items[] = {{.name = "BMW/Mini V3", .get_string = bmw_v3_decode}};
static const SubGhzDecodeRegistry bmw_v3_registry = {.items = bmw_v3_items, .size = 1};
static const FlipperAppPluginDescriptor decode_bmw_v3_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &bmw_v3_registry};
const FlipperAppPluginDescriptor* decode_bmw_v3_ep(void) { return &decode_bmw_v3_descriptor; }
