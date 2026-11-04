/* decode_renault_v3.fal : SD get_string for renault_v3 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/blocks/bitbuf.h>
#include <lib/subghz/protocols/renault_v3.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define RENAULT_V3_ARR_SIZE      256
#define RENAULT_V3_FRAME_BYTES   20 // 160 bits

struct SubGhzProtocolDecoderRenaultV3 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t step;
    uint32_t preamble_count;
    uint8_t all_bits[SUBGHZ_BITBUF_BYTES(RENAULT_V3_ARR_SIZE)];
    uint16_t all_bit_count;

    uint16_t serial;
    uint8_t variant; // 0 = TypeA, 1 = TypeB
    uint8_t frame_bytes[RENAULT_V3_FRAME_BYTES]; // full MSB-first frame from sync
    uint16_t frame_bit_len;
};

static void renault_v3_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderRenaultV3* instance = input->decoder;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Serial:%04X  %s\r\n"
        "Encrypted (no CRC)\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)(instance->generic.data >> 32),
        (unsigned long)(instance->generic.data & 0xFFFFFFFFU),
        instance->serial,
        instance->variant ? "TypeB" : "TypeA");
}

static const SubGhzDecodeEntry renault_v3_items[] = {{.name = "Renault V3", .get_string = renault_v3_decode}};
static const SubGhzDecodeRegistry renault_v3_registry = {.items = renault_v3_items, .size = 1};
static const FlipperAppPluginDescriptor decode_renault_v3_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &renault_v3_registry};
const FlipperAppPluginDescriptor* decode_renault_v3_ep(void) { return &decode_renault_v3_descriptor; }
