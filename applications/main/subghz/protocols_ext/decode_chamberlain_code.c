/* decode_chamberlain_code.fal : SD get_string for chamberlain_code (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/chamberlain_code.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define CHAMBERLAIN_7_CODE_DIP_PATTERN "%c%c%c%c%c%c%c"
#define CHAMBERLAIN_7_CODE_DATA_TO_DIP(dip)                                                 \
    (dip & 0x0040 ? '1' : '0'), (dip & 0x0020 ? '1' : '0'), (dip & 0x0010 ? '1' : '0'),     \
        (dip & 0x0008 ? '1' : '0'), (dip & 0x0004 ? '1' : '0'), (dip & 0x0002 ? '1' : '0'), \
        (dip & 0x0001 ? '1' : '0')
#define CHAMBERLAIN_8_CODE_DIP_PATTERN "%c%c%c%c%cx%c%c"
#define CHAMBERLAIN_8_CODE_DATA_TO_DIP(dip)                                                 \
    (dip & 0x0080 ? '1' : '0'), (dip & 0x0040 ? '1' : '0'), (dip & 0x0020 ? '1' : '0'),     \
        (dip & 0x0010 ? '1' : '0'), (dip & 0x0008 ? '1' : '0'), (dip & 0x0001 ? '1' : '0'), \
        (dip & 0x0002 ? '1' : '0')
#define CHAMBERLAIN_9_CODE_DIP_PATTERN "%c%c%c%c%c%c%c%c%c"
#define CHAMBERLAIN_9_CODE_DATA_TO_DIP(dip)                                                 \
    (dip & 0x0100 ? '1' : '0'), (dip & 0x0080 ? '1' : '0'), (dip & 0x0040 ? '1' : '0'),     \
        (dip & 0x0020 ? '1' : '0'), (dip & 0x0010 ? '1' : '0'), (dip & 0x0008 ? '1' : '0'), \
        (dip & 0x0001 ? '1' : '0'), (dip & 0x0002 ? '1' : '0'), (dip & 0x0004 ? '1' : '0')

struct SubGhzProtocolDecoderChamb_Code {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static void chamberlain_code_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderChamb_Code* instance = input->decoder;


    uint32_t code_found_lo = instance->generic.data & 0x00000000ffffffff;

    uint64_t code_found_reverse = subghz_protocol_blocks_reverse_key(
        instance->generic.data, instance->generic.data_count_bit);

    uint32_t code_found_reverse_lo = code_found_reverse & 0x00000000ffffffff;

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "Key:0x%03lX\r\n"
        "Yek:0x%03lX\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        code_found_lo,
        code_found_reverse_lo);

    switch(instance->generic.data_count_bit) {
    case 7:
        furi_string_cat_printf(
            output,
            "DIP:" CHAMBERLAIN_7_CODE_DIP_PATTERN "\r\n",
            CHAMBERLAIN_7_CODE_DATA_TO_DIP(code_found_lo));
        break;
    case 8:
        furi_string_cat_printf(
            output,
            "DIP:" CHAMBERLAIN_8_CODE_DIP_PATTERN "\r\n",
            CHAMBERLAIN_8_CODE_DATA_TO_DIP(code_found_lo));
        break;
    case 9:
        furi_string_cat_printf(
            output,
            "DIP:" CHAMBERLAIN_9_CODE_DIP_PATTERN "\r\n",
            CHAMBERLAIN_9_CODE_DATA_TO_DIP(code_found_lo));
        break;

    default:
        break;
    }
}

static const SubGhzDecodeEntry chamberlain_code_items[] = {{.name = "Cham_Code", .get_string = chamberlain_code_decode}};
static const SubGhzDecodeRegistry chamberlain_code_registry = {.items = chamberlain_code_items, .size = 1};
static const FlipperAppPluginDescriptor decode_chamberlain_code_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &chamberlain_code_registry};
const FlipperAppPluginDescriptor* decode_chamberlain_code_ep(void) { return &decode_chamberlain_code_descriptor; }
