/* decode_came.fal : SD get_string for came (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/came.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define PRASTEL_25_COUNT_BIT 25
#define PRASTEL_42_COUNT_BIT 42
#define PRASTEL_NAME         "Prastel"
#define AIRFORCE_COUNT_BIT   18
#define AIRFORCE_NAME        "Airforce"

struct SubGhzProtocolDecoderCame {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static void came_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderCame* instance = input->decoder;


    uint32_t code_found_lo = instance->generic.data & 0x000003ffffffffff;

    uint64_t code_found_reverse = subghz_protocol_blocks_reverse_key(
        instance->generic.data, instance->generic.data_count_bit);

    uint32_t code_found_reverse_lo = code_found_reverse & 0x000003ffffffffff;

    const char* name = instance->generic.protocol_name;
    switch(instance->generic.data_count_bit) {
    case PRASTEL_25_COUNT_BIT:
    case PRASTEL_42_COUNT_BIT:
        name = PRASTEL_NAME;
        break;
    case AIRFORCE_COUNT_BIT:
        name = AIRFORCE_NAME;
        break;
    }

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%08lX\r\n"
        "Yek:0x%08lX\r\n",
        name,
        instance->generic.data_count_bit,
        code_found_lo,
        code_found_reverse_lo);
}

static const SubGhzDecodeEntry came_items[] = {{.name = "CAME", .get_string = came_decode}};
static const SubGhzDecodeRegistry came_registry = {.items = came_items, .size = 1};
static const FlipperAppPluginDescriptor decode_came_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &came_registry};
const FlipperAppPluginDescriptor* decode_came_ep(void) { return &decode_came_descriptor; }
