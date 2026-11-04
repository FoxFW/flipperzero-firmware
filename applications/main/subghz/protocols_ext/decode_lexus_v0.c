/* decode_lexus_v0.fal : SD get_string for lexus_v0 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/blocks/bitbuf.h>
#include <lib/subghz/protocols/lexus_v0.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define LEXUS_V0_FRAME_BITS 224
#define LEXUS_V0_DATA_BYTES 20

struct SubGhzProtocolDecoderLexusV0 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t step; // 0 reset, 1 preamble, 2 data
    uint16_t preamble_count;
    bool half_bit;
    uint16_t bit_count;
    uint8_t all_bits[SUBGHZ_BITBUF_BYTES(LEXUS_V0_FRAME_BITS + 8)];

    uint8_t rolling[LEXUS_V0_DATA_BYTES]; // full 160-bit payload
    uint32_t serial;

    // Toyota complement-signature cross-frame validator.
    uint32_t prev_serial;
    bool have_prev;
    bool compl_ok;
};

static void lexus_v0_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderLexusV0* instance = input->decoder;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Serial:%08lX\r\n"
        "Btn:(encrypted)\r\n"
        "Compl:%s\r\n"
        "RC:",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)instance->serial,
        instance->compl_ok ? "OK" : "--");
    for(int k = 0; k < LEXUS_V0_DATA_BYTES; k++) {
        furi_string_cat_printf(output, "%02X", instance->rolling[k]);
    }
    furi_string_cat_printf(output, "\r\n");
}

static const SubGhzDecodeEntry lexus_v0_items[] = {{.name = "Lexus V0", .get_string = lexus_v0_decode}};
static const SubGhzDecodeRegistry lexus_v0_registry = {.items = lexus_v0_items, .size = 1};
static const FlipperAppPluginDescriptor decode_lexus_v0_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &lexus_v0_registry};
const FlipperAppPluginDescriptor* decode_lexus_v0_ep(void) { return &decode_lexus_v0_descriptor; }
