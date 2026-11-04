/* decode_mitsubishi_v1.fal : SD get_string for mitsubishi_v1 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/mitsubishi_v1.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define MITSUBISHI_V1_ALL_BITS_MAX 200

struct SubGhzProtocolDecoderMitsubishiV1 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t step; // 0 = Reset, 1 = Collect
    bool half;
    uint8_t all_bits[MITSUBISHI_V1_ALL_BITS_MAX];
    uint16_t bit_count;

    uint8_t fix;
    uint8_t btn;
    uint16_t cnt;
    uint64_t hop; // 42-bit encrypted rolling code
};

static const char* mitsubishi_v1_button_name(uint8_t btn);

static const char* mitsubishi_v1_button_name(uint8_t btn) {
    switch(btn & 0x0F) {
    case 0x2: return "Lock";
    case 0x8: return "Unlock";
    case 0x4: return "Trunk";
    default: return "??";
    }
}

static void mitsubishi_v1_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderMitsubishiV1* instance = input->decoder;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Fix:%02X Btn:%X %s\r\n"
        "Cnt:%03X  Hop:%011llX\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)(instance->generic.data >> 32),
        (unsigned long)(instance->generic.data & 0xFFFFFFFFU),
        instance->fix,
        instance->btn,
        mitsubishi_v1_button_name(instance->btn),
        instance->cnt,
        (unsigned long long)instance->hop);
}

static const SubGhzDecodeEntry mitsubishi_v1_items[] = {{.name = "Mitsubishi V1", .get_string = mitsubishi_v1_decode}};
static const SubGhzDecodeRegistry mitsubishi_v1_registry = {.items = mitsubishi_v1_items, .size = 1};
static const FlipperAppPluginDescriptor decode_mitsubishi_v1_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &mitsubishi_v1_registry};
const FlipperAppPluginDescriptor* decode_mitsubishi_v1_ep(void) { return &decode_mitsubishi_v1_descriptor; }
