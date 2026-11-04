/* decode_linear_delta3.fal : SD get_string for linear_delta3 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/linear_delta3.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define DIP_PATTERN "%c%c%c%c%c%c%c%c"
#define DATA_TO_DIP(dip)                                                                    \
    (dip & 0x0080 ? '0' : '1'), (dip & 0x0040 ? '0' : '1'), (dip & 0x0020 ? '0' : '1'),     \
        (dip & 0x0010 ? '0' : '1'), (dip & 0x0008 ? '0' : '1'), (dip & 0x0004 ? '0' : '1'), \
        (dip & 0x0002 ? '0' : '1'), (dip & 0x0001 ? '0' : '1')

struct SubGhzProtocolDecoderLinearDelta3 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t last_data;
};

static void linear_delta3_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderLinearDelta3* instance = input->decoder;


    uint32_t data = instance->generic.data & 0xFF;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%lX\r\n"
        "DIP:" DIP_PATTERN "\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        data,
        DATA_TO_DIP(data));
}

static const SubGhzDecodeEntry linear_delta3_items[] = {{.name = "LinearDelta3", .get_string = linear_delta3_decode}};
static const SubGhzDecodeRegistry linear_delta3_registry = {.items = linear_delta3_items, .size = 1};
static const FlipperAppPluginDescriptor decode_linear_delta3_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &linear_delta3_registry};
const FlipperAppPluginDescriptor* decode_linear_delta3_ep(void) { return &decode_linear_delta3_descriptor; }
