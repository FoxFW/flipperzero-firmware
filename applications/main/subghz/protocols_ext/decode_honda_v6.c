/* decode_honda_v6.fal : SD get_string for honda_v6 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/honda_v6.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderHondaV6 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t parser_step; // 0 reset, 1 checkpre, 2 collect
    uint32_t header;
    uint8_t man_state;
    uint64_t lo;
    uint64_t hi;
    uint16_t bit_count;
    bool midgap;
    uint16_t halfa;

    uint32_t serial;
    uint8_t btn;
    uint32_t cnt;
    uint8_t variant; // 0 stock45, 1 pandora, 2 313 (3-level trit)
    uint64_t data_hi; // pandora high word / 313 d1 (trits 32..63)

    // 313 MHz 3-level "trit" mode state
    uint16_t trit_count;
    uint64_t d2; // trits 0..31 packed 2 bits each, MSB-first
    uint64_t d1; // trits 32..63
};

static const char* honda_v6_btn_name(uint8_t btn);

static const char* honda_v6_btn_name(uint8_t btn) {
    switch(btn) {
    case 0x01: return "Lock";
    case 0x02: return "Unlock";
    case 0x03: return "Trunk";
    case 0x10: return "BCM";
    default: return "?";
    }
}

static void honda_v6_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderHondaV6* instance = input->decoder;

    if(instance->variant == 0) {
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "CRC:???\r\n"
            "Key:%012llX\r\n"
            "Serial:%08lX Btn:%X %s\r\n"
            "Cnt:%X\r\n",
            instance->generic.protocol_name,
            instance->generic.data_count_bit,
            (unsigned long long)instance->generic.data,
            (unsigned long)instance->serial,
            instance->btn,
            honda_v6_btn_name(instance->btn),
            (unsigned)instance->cnt);
    } else if(instance->variant == 2) {
        // 313 MHz 3-level trit: generic.data = d2 (trits 0..31), data_hi = d1 (32..63).
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "CRC:???\r\n"
            "Key:%016llX%016llX\r\n"
            "Serial:%08lX Cnt:%lu\r\n",
            instance->generic.protocol_name,
            instance->generic.data_count_bit,
            (unsigned long long)instance->generic.data,
            (unsigned long long)instance->data_hi,
            (unsigned long)instance->serial,
            (unsigned long)instance->cnt);
    } else {
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "CRC:???\r\n"
            "Key:%016llX%016llX\r\n"
            "Serial:%08lX Cnt:%lu\r\n",
            instance->generic.protocol_name,
            instance->generic.data_count_bit,
            (unsigned long long)instance->data_hi,
            (unsigned long long)instance->generic.data,
            (unsigned long)instance->serial,
            (unsigned long)instance->cnt);
    }
}

static const SubGhzDecodeEntry honda_v6_items[] = {{.name = "Honda V6", .get_string = honda_v6_decode}};
static const SubGhzDecodeRegistry honda_v6_registry = {.items = honda_v6_items, .size = 1};
static const FlipperAppPluginDescriptor decode_honda_v6_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &honda_v6_registry};
const FlipperAppPluginDescriptor* decode_honda_v6_ep(void) { return &decode_honda_v6_descriptor; }
