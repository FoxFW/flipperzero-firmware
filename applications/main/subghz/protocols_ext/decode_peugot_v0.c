/* decode_peugot_v0.fal : SD get_string for peugot_v0 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/blocks/bitbuf.h>
#include <lib/subghz/protocols/peugot_v0.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <furi.h>
#include <furi_hal.h>

#define PEUGOT_V0_MAX_BITS          256

struct SubGhzProtocolDecoderPeugotV0 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t all_bits[SUBGHZ_BITBUF_BYTES(PEUGOT_V0_MAX_BITS)];
    uint16_t bit_count;
    bool half_bit;

    uint16_t data_16;  // Bits 64-79 of the 80-bit frame
};

static void peugot_v0_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderPeugotV0* instance = input->decoder;


    // First 16 bits of data = Serial, remaining 48 bits + Key2 (16 bits) = 64-bit Hop
    uint16_t serial = (uint16_t)(instance->generic.data >> 48);
    uint32_t hop_hi = (uint32_t)((instance->generic.data >> 16) & 0xFFFFFFFF);
    uint16_t hop_lo_part = (uint16_t)(instance->generic.data & 0xFFFF);
    // Full hop: 48 bits from generic.data (bits 47..0) + 16 bits from data_16 = 64 bits

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Key2:%04X Sn:%04X\r\n"
        "Hop:%08lX%04X%04X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)(instance->generic.data >> 32),
        (unsigned long)(instance->generic.data & 0xFFFFFFFF),
        instance->data_16,
        serial,
        (unsigned long)hop_hi,
        hop_lo_part,
        instance->data_16);
}

static const SubGhzDecodeEntry peugot_v0_items[] = {{.name = "Peugot V0", .get_string = peugot_v0_decode}};
static const SubGhzDecodeRegistry peugot_v0_registry = {.items = peugot_v0_items, .size = 1};
static const FlipperAppPluginDescriptor decode_peugot_v0_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &peugot_v0_registry};
const FlipperAppPluginDescriptor* decode_peugot_v0_ep(void) { return &decode_peugot_v0_descriptor; }
