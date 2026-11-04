/* decode_audi_v3.fal : SD get_string for audi_v3 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/audi_v3.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define AUDI_V3_DATA_BITS 96

struct SubGhzProtocolDecoderAudiV3 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t parser_step;
    uint16_t pre;
    bool half;
    uint8_t bits[AUDI_V3_DATA_BITS];
    uint16_t nbits;

    uint8_t button;
    uint8_t hop[7];
    uint32_t counter; // 24-bit
    uint8_t chk;
};

static const char* audi_v3_button_name(uint8_t button);

static const char* audi_v3_button_name(uint8_t button) {
    switch(button) {
    case 0x0C: return "Unlock";
    case 0x0D: return "Lock";
    default: return "?";
    }
}

static void audi_v3_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderAudiV3* instance = input->decoder;

    furi_string_cat_printf(
        output,
        "%s %dbit (FM)\r\n"
        "Key:%08lX%08lX\r\n"
        "Btn:%02X %s\r\n"
        "Cnt:%06lX Chk:%02X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)(instance->generic.data >> 32),
        (unsigned long)(instance->generic.data & 0xFFFFFFFFU),
        instance->button,
        audi_v3_button_name(instance->button),
        (unsigned long)instance->counter,
        instance->chk);
}

static const SubGhzDecodeEntry audi_v3_items[] = {{.name = "Audi V3", .get_string = audi_v3_decode}};
static const SubGhzDecodeRegistry audi_v3_registry = {.items = audi_v3_items, .size = 1};
static const FlipperAppPluginDescriptor decode_audi_v3_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &audi_v3_registry};
const FlipperAppPluginDescriptor* decode_audi_v3_ep(void) { return &decode_audi_v3_descriptor; }
