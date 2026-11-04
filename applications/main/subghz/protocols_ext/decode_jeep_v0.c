/* decode_jeep_v0.fal : SD get_string for jeep_v0 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/jeep_v0.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>

struct SubGhzProtocolDecoderJeepV0 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t parser_step;
    uint32_t te_last;
    uint16_t preamble_count;
    ManchesterState manchester_state;

    uint64_t data_lo; // first 64 demodulated bits (serial|btn|0x07|cnt), post-invert
    uint32_t data_32; // last 32 demodulated bits (hop), post-invert
    uint16_t bit_count;

    uint32_t serial;
    uint16_t btn_code;
    uint8_t b6;
    uint8_t cnt;
    uint32_t hop;
    bool frame_ok;
};

static const char* jeep_v0_button_name(uint16_t code);

static const char* jeep_v0_button_name(uint16_t code) {
    switch(code) {
    case 0x8104: return "Unlock";
    case 0x8044: return "Lock";
    case 0x8804: return "RS";
    case 0x8024: return "Panic";
    default: return "?";
    }
}

static void jeep_v0_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderJeepV0* instance = input->decoder;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Serial:%08lX\r\n"
        "Btn:%04X %s\r\n"
        "Cnt:%02X  Hop:%08lX\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)instance->serial,
        instance->btn_code,
        jeep_v0_button_name(instance->btn_code),
        instance->cnt,
        (unsigned long)instance->hop);
}

static const SubGhzDecodeEntry jeep_v0_items[] = {{.name = "Jeep V0", .get_string = jeep_v0_decode}};
static const SubGhzDecodeRegistry jeep_v0_registry = {.items = jeep_v0_items, .size = 1};
static const FlipperAppPluginDescriptor decode_jeep_v0_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &jeep_v0_registry};
const FlipperAppPluginDescriptor* decode_jeep_v0_ep(void) { return &decode_jeep_v0_descriptor; }
