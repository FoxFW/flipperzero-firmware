/* decode_honda_v4.fal : SD get_string for honda_v4 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/honda_v4.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderHondaV4 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t parser_step;
    uint32_t pre;
    uint32_t te_last;
    uint64_t data;
    uint32_t cnt_bits;

    uint32_t serial;
    uint8_t btn;
    uint16_t cnt;
    bool crc_ok;
};

static const char* honda_v4_button_name(uint8_t btn);

static const char* honda_v4_button_name(uint8_t btn) {
    switch(btn) {
    case 0x11:
        return "Lock";
    case 0x12:
        return "Unlock";
    default:
        return "?";
    }
}

static void honda_v4_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderHondaV4* instance = input->decoder;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Serial:%05lX Btn:%02X %s\r\n"
        "Cnt:%04X  Inv:%s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)(uint32_t)(instance->generic.data >> 32),
        (unsigned long)(uint32_t)(instance->generic.data & 0xFFFFFFFFU),
        (unsigned long)instance->serial,
        instance->btn,
        honda_v4_button_name(instance->btn),
        instance->cnt,
        instance->crc_ok ? "OK" : "--");
}

static const SubGhzDecodeEntry honda_v4_items[] = {{.name = "Honda V4", .get_string = honda_v4_decode}};
static const SubGhzDecodeRegistry honda_v4_registry = {.items = honda_v4_items, .size = 1};
static const FlipperAppPluginDescriptor decode_honda_v4_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &honda_v4_registry};
const FlipperAppPluginDescriptor* decode_honda_v4_ep(void) { return &decode_honda_v4_descriptor; }
