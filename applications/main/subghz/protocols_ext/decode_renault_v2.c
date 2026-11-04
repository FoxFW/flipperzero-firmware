/* decode_renault_v2.fal : SD get_string for renault_v2 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/renault_v2.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define RENAULT_V2_ARR_SIZE    200

struct SubGhzProtocolDecoderRenaultV2 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t step;
    bool half_bit;
    uint8_t all_bits[RENAULT_V2_ARR_SIZE];
    uint16_t all_bit_count;

    uint32_t data_tail; // low 18 bits of the 82-bit frame (bits 64..81)
};

static void renault_v2_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderRenaultV2* instance = input->decoder;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Tail:%05lX\r\n"
        "Sync:71EA Encrypted\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)(instance->generic.data >> 32),
        (unsigned long)(instance->generic.data & 0xFFFFFFFFU),
        (unsigned long)instance->data_tail);
}

static const SubGhzDecodeEntry renault_v2_items[] = {{.name = "Renault V2", .get_string = renault_v2_decode}};
static const SubGhzDecodeRegistry renault_v2_registry = {.items = renault_v2_items, .size = 1};
static const FlipperAppPluginDescriptor decode_renault_v2_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &renault_v2_registry};
const FlipperAppPluginDescriptor* decode_renault_v2_ep(void) { return &decode_renault_v2_descriptor; }
