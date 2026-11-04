/* decode_renault_v4.fal : SD get_string for renault_v4 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/renault_v4.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define RENAULT_V4_ARR_SIZE   128

struct SubGhzProtocolDecoderRenaultV4 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    bool synced;
    uint8_t man_state;
    uint8_t bits[RENAULT_V4_ARR_SIZE];
    uint16_t bit_count;

    uint8_t last_key[11];
    bool last_valid;

    uint32_t serial;
    uint8_t btn;
    uint8_t cnt; // cleartext 8-bit counter
    uint32_t data_tail; // low 24 bits of the 88-bit frame (bits 64..87)
    bool crc_ok;
};

static const char* renault_v4_button_name(uint8_t btn_nib);

static const char* renault_v4_button_name(uint8_t btn_nib) {
    switch(btn_nib) {
    case 0xE: return "E";
    case 0xD: return "D";
    case 0xB: return "B";
    case 0x7: return "7";
    default: return "?";
    }
}

static void renault_v4_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderRenaultV4* instance = input->decoder;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Serial:%08lX\r\n"
        "Btn:%X(%s)  Cnt:%02X\r\n"
        "Tail:%06lX  Chk:%s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)(instance->generic.data >> 32),
        (unsigned long)(instance->generic.data & 0xFFFFFFFFU),
        (unsigned long)instance->serial,
        instance->btn,
        renault_v4_button_name(instance->btn),
        instance->cnt,
        (unsigned long)instance->data_tail,
        instance->crc_ok ? "OK" : "--");
}

static const SubGhzDecodeEntry renault_v4_items[] = {{.name = "Renault/xHorse V4", .get_string = renault_v4_decode}};
static const SubGhzDecodeRegistry renault_v4_registry = {.items = renault_v4_items, .size = 1};
static const FlipperAppPluginDescriptor decode_renault_v4_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &renault_v4_registry};
const FlipperAppPluginDescriptor* decode_renault_v4_ep(void) { return &decode_renault_v4_descriptor; }
