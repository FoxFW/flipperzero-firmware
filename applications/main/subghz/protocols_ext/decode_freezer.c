

#include "subghz_decode_plugin_desc.h"
#include <lib/subghz/blocks/generic.h>
#include <furi.h>

static void decode_freezer_get_string(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->generic) return;
    SubGhzBlockGeneric* generic = input->generic;
    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%06lX\r\n"
        "Sn:0x%05lX Btn:%X\r\n",
        generic->protocol_name,
        generic->data_count_bit,
        (unsigned long)(generic->data & 0xFFFFFF),
        (unsigned long)generic->serial,
        generic->btn);
}

static const SubGhzDecodeEntry decode_freezer_items[] = {
    {.name = "Freezer", .get_string = decode_freezer_get_string},
};
static const SubGhzDecodeRegistry decode_freezer_registry = {
    .items = decode_freezer_items,
    .size = 1,
};
static const FlipperAppPluginDescriptor decode_freezer_descriptor = {
    .appid = SUBGHZ_DECODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION,
    .entry_point = &decode_freezer_registry,
};
const FlipperAppPluginDescriptor* decode_freezer_ep(void) {
    return &decode_freezer_descriptor;
}
