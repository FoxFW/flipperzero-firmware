/* decode_ducato_v0.fal : SD get_string for ducato_v0 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/ducato_v0.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderDucatoV0 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t parser_step;
    uint32_t te_last;
    uint16_t preamble_count;
    uint8_t man_state;

    uint64_t data_lo; // bits 0..63
    uint32_t data_hi; // bits 64..70 (7 bits)
    uint16_t bit_count;

    uint32_t fix;
    uint32_t hop;
    uint32_t serial;
    uint8_t btn;
    uint8_t crc;
    bool crc_ok;
};

static const char* ducato_v0_button_name(uint8_t btn);

static const char* ducato_v0_button_name(uint8_t btn) {
    switch(btn) {
    case 0x01: return "LOCK";
    case 0x02: return "UNLOCK";
    case 0x06: return "TRUNK";
    case 0x09: return "PANIC";
    default: return "?";
    }
}

static void ducato_v0_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderDucatoV0* instance = input->decoder;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Serial:%06lX Btn:%02X\r\n"
        "%s  Hop:%08lX\r\n"
        "CRC:%02X %s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)instance->hop,
        (unsigned long)instance->fix,
        (unsigned long)instance->serial,
        instance->btn,
        ducato_v0_button_name(instance->btn),
        (unsigned long)instance->hop,
        instance->crc,
        instance->crc_ok ? "OK" : "--");
}

static const SubGhzDecodeEntry ducato_v0_items[] = {{.name = "Fiat Ducato V0", .get_string = ducato_v0_decode}};
static const SubGhzDecodeRegistry ducato_v0_registry = {.items = ducato_v0_items, .size = 1};
static const FlipperAppPluginDescriptor decode_ducato_v0_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &ducato_v0_registry};
const FlipperAppPluginDescriptor* decode_ducato_v0_ep(void) { return &decode_ducato_v0_descriptor; }
