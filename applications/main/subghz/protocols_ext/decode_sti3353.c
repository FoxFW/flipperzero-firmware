

#include "subghz_decode_plugin_desc.h"
#include <lib/subghz/blocks/generic.h>
#include <furi.h>

static void decode_sti3353_get_string(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->generic) return;
    SubGhzBlockGeneric* generic = input->generic;
    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key: 0x%04lX%08lX\r\n",
        generic->protocol_name,
        generic->data_count_bit,
        (unsigned long)((generic->data >> 32) & 0xFFFF),
        (unsigned long)(generic->data & 0xFFFFFFFF));
}

static const SubGhzDecodeEntry decode_sti3353_items[] = {
    {.name = "STI-3353", .get_string = decode_sti3353_get_string},
};
static const SubGhzDecodeRegistry decode_sti3353_registry = {
    .items = decode_sti3353_items,
    .size = 1,
};
static const FlipperAppPluginDescriptor decode_sti3353_descriptor = {
    .appid = SUBGHZ_DECODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION,
    .entry_point = &decode_sti3353_registry,
};
const FlipperAppPluginDescriptor* decode_sti3353_ep(void) {
    return &decode_sti3353_descriptor;
}
