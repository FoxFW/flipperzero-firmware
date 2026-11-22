

#include "subghz_decode_plugin_desc.h"
#include <lib/subghz/blocks/generic.h>
#include <furi.h>

static void decode_hcs362_get_string(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->generic) return;
    SubGhzBlockGeneric* generic = input->generic;
    const char* mfr = (input->aux && input->aux[0]) ? input->aux : "Unknown";
    furi_string_cat_printf(
        output,
        "%s 69bit\r\n"
        "[KeeLoq (%s)]\r\n"
        "Ser:0x%07lX Btn:%X\r\n"
        "Cnt:%04lX\r\n",
        generic->protocol_name,
        mfr,
        (unsigned long)(generic->serial & 0x0FFFFFFF),
        generic->btn,
        (unsigned long)(generic->cnt & 0xFFFF));
}

static const SubGhzDecodeEntry decode_hcs362_items[] = {
    {.name = "Microchip-HCS362", .get_string = decode_hcs362_get_string},
};
static const SubGhzDecodeRegistry decode_hcs362_registry = {
    .items = decode_hcs362_items,
    .size = 1,
};
static const FlipperAppPluginDescriptor decode_hcs362_descriptor = {
    .appid = SUBGHZ_DECODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION,
    .entry_point = &decode_hcs362_registry,
};
const FlipperAppPluginDescriptor* decode_hcs362_ep(void) {
    return &decode_hcs362_descriptor;
}
