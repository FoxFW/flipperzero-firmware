
#include "subghz_decode_plugin_desc.h"
#include <lib/subghz/blocks/generic.h>
#include <furi.h>
static void decode_cardin_s508_get_string(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->generic) return;
    SubGhzBlockGeneric* g = input->generic;
    furi_string_cat_printf(
        output,
        "%s\r\n"
        "Rolling (AES-128)\r\n"
        "Sync:110011110100\r\n"
        "Key:%08lX%08lX%08lX%08lX\r\n",
        g->protocol_name,
        (unsigned long)(g->data_2 >> 32),
        (unsigned long)(g->data_2 & 0xFFFFFFFF),
        (unsigned long)(g->data >> 32),
        (unsigned long)(g->data & 0xFFFFFFFF));
}
static const SubGhzDecodeEntry decode_cardin_s508_items[] = {
    {.name = "Cardin S508", .get_string = decode_cardin_s508_get_string}};
static const SubGhzDecodeRegistry decode_cardin_s508_registry = {
    .items = decode_cardin_s508_items, .size = 1};
static const FlipperAppPluginDescriptor decode_cardin_s508_descriptor = {
    .appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION,
    .entry_point = &decode_cardin_s508_registry};
const FlipperAppPluginDescriptor* decode_cardin_s508_ep(void) { return &decode_cardin_s508_descriptor; }
