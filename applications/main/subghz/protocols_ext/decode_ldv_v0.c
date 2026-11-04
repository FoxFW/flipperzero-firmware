/* decode_ldv_v0.fal : SD get_string for ldv_v0 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/ldv_v0.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>

struct SubGhzProtocolDecoderLdvV0 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t parser_step;
    ManchesterState manchester_state;

    uint16_t preamble_count;
    uint8_t last_bit_value;

    uint64_t data_2; // second 64 demodulated bits (bytes 8..15)
    uint16_t bit_count;

    uint32_t serial;
    uint8_t btn;
    uint32_t cnt;
    uint64_t hop; // 40-bit rolling tail
    bool frame_ok;
};

static const char* ldv_v0_button_name(uint8_t btn);

static const char* ldv_v0_button_name(uint8_t btn) {
    switch(btn) {
    case 1: return "Unlock";
    case 2: return "Lock";
    case 3: return "Trunk";
    default: return "?";
    }
}

static void ldv_v0_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderLdvV0* instance = input->decoder;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Serial:%08lX\r\n"
        "Btn:%X %s  Cnt:%04lX\r\n"
        "Hop:%010llX\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)instance->serial,
        instance->btn,
        ldv_v0_button_name(instance->btn),
        (unsigned long)instance->cnt,
        (unsigned long long)instance->hop);
}

static const SubGhzDecodeEntry ldv_v0_items[] = {{.name = "LDV V0", .get_string = ldv_v0_decode}};
static const SubGhzDecodeRegistry ldv_v0_registry = {.items = ldv_v0_items, .size = 1};
static const FlipperAppPluginDescriptor decode_ldv_v0_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &ldv_v0_registry};
const FlipperAppPluginDescriptor* decode_ldv_v0_ep(void) { return &decode_ldv_v0_descriptor; }
