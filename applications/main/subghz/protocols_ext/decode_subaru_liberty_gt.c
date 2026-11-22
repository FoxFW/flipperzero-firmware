

#include "subghz_decode_plugin_desc.h"
#include <lib/subghz/blocks/generic.h>
#include <furi.h>

static void decode_subaru_liberty_gt_get_string(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->generic) return;
    SubGhzBlockGeneric* generic = input->generic;
    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Serial: 0x%08lX\r\n"
        "Key: 0x%08lX%08lX\r\n",
        generic->protocol_name,
        generic->data_count_bit,
        (unsigned long)generic->serial,
        (unsigned long)(generic->data >> 32),
        (unsigned long)(generic->data & 0xFFFFFFFF));
}

static const SubGhzDecodeEntry decode_subaru_liberty_gt_items[] = {
    {.name = "Subaru Liberty GT", .get_string = decode_subaru_liberty_gt_get_string},
};
static const SubGhzDecodeRegistry decode_subaru_liberty_gt_registry = {
    .items = decode_subaru_liberty_gt_items,
    .size = 1,
};
static const FlipperAppPluginDescriptor decode_subaru_liberty_gt_descriptor = {
    .appid = SUBGHZ_DECODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION,
    .entry_point = &decode_subaru_liberty_gt_registry,
};
const FlipperAppPluginDescriptor* decode_subaru_liberty_gt_ep(void) {
    return &decode_subaru_liberty_gt_descriptor;
}
