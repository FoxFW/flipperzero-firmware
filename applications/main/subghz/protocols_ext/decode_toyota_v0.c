/* decode_toyota_v0.fal : SD get_string for toyota_v0 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/toyota_v0.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <furi.h>
#include <furi_hal.h>

#define TOYOTA_V0_FRAME_BITS      184   // 88 data + 8 zero-gap + 88 complement

struct SubGhzProtocolDecoderToyotaV0 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t preamble_count;
    bool half_bit;
    uint8_t all_bits[TOYOTA_V0_FRAME_BITS + 8];
    uint16_t bit_count;

    uint32_t data_32; // bits 64-95 (last 4 bytes)
};

static void toyota_v0_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderToyotaV0* instance = input->decoder;


    uint32_t key_hi = (uint32_t)(instance->generic.data >> 32);
    uint32_t key_lo = (uint32_t)(instance->generic.data & 0xFFFFFFFF);

    // data_32 holds bits 64-87 left-aligned (top 24 bits used)
    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Key2:%06lX\r\n"
        "Hop:%08lX%08lX%06lX",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)key_hi,
        (unsigned long)key_lo,
        (unsigned long)(instance->data_32 >> 8),
        (unsigned long)key_hi,
        (unsigned long)key_lo,
        (unsigned long)(instance->data_32 >> 8));
}

static const SubGhzDecodeEntry toyota_v0_items[] = {{.name = "Toyota V0", .get_string = toyota_v0_decode}};
static const SubGhzDecodeRegistry toyota_v0_registry = {.items = toyota_v0_items, .size = 1};
static const FlipperAppPluginDescriptor decode_toyota_v0_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &toyota_v0_registry};
const FlipperAppPluginDescriptor* decode_toyota_v0_ep(void) { return &decode_toyota_v0_descriptor; }
