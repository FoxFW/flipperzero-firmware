/* decode_honda_v5.fal : SD get_string for honda_v5 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/blocks/bitbuf.h>
#include <lib/subghz/protocols/honda_v5.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <string.h>

#define HONDA_V5_MAX_BITS 400

struct SubGhzProtocolDecoderHondaV5 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t parser_step;
    uint8_t man_state;
    uint8_t bits[SUBGHZ_BITBUF_BYTES(HONDA_V5_MAX_BITS)];
    uint16_t bit_count;

    uint16_t variant; // 135 or 168
    uint64_t data_mid; // frame bytes 8..15
    uint32_t data_ext; // frame bytes 16..19 (alt only)
    uint32_t serial;
    uint8_t btn;
    uint16_t cnt;
    uint32_t rolling;
    uint8_t crc;
    bool crc_ok;
    bool rtx;
};

static const char* honda_v5_button_name(uint8_t btn);

static const char* honda_v5_button_name(uint8_t btn) {
    switch(btn) {
    case 0x21:
        return "Lock";
    case 0x22:
        return "Unlock";
    default:
        return "?";
    }
}

static void honda_v5_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderHondaV5* instance = input->decoder;

    furi_string_cat_printf(
        output,
        "%s %dbit (%d)\r\n"
        "Key:%08lX%08lX\r\n"
        "Serial:%08lX Btn:%02X %s\r\n"
        "Cnt:%04X Roll:%08lX %s\r\n"
        "CRC:%02X %s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        instance->variant,
        (unsigned long)(uint32_t)(instance->generic.data >> 32),
        (unsigned long)(uint32_t)(instance->generic.data & 0xFFFFFFFFU),
        (unsigned long)instance->serial,
        instance->btn,
        honda_v5_button_name(instance->btn),
        instance->cnt,
        (unsigned long)instance->rolling,
        instance->rtx ? "RTX" : "TX",
        instance->crc,
        instance->crc_ok ? "OK" : "--");
}

static const SubGhzDecodeEntry honda_v5_items[] = {{.name = "Honda V5", .get_string = honda_v5_decode}};
static const SubGhzDecodeRegistry honda_v5_registry = {.items = honda_v5_items, .size = 1};
static const FlipperAppPluginDescriptor decode_honda_v5_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &honda_v5_registry};
const FlipperAppPluginDescriptor* decode_honda_v5_ep(void) { return &decode_honda_v5_descriptor; }
