
#include "subghz_decode_plugin_desc.h"
#include <lib/subghz/blocks/generic.h>
#include <furi.h>
static void decode_prastel_get_string(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->generic) return;
    SubGhzBlockGeneric* g = input->generic;
    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%011llX\r\n"
        "Sn:%08lX\r\n"
        "Btn:%01X\r\n"
        "Cnt:%04lX\r\n",
        g->protocol_name, g->data_count_bit, (unsigned long long)(g->data & 0x3FFFFFFFFFFULL),
        (unsigned long)g->serial, g->btn, (unsigned long)g->cnt);
}
static const SubGhzDecodeEntry decode_prastel_items[] = {
    {.name = "Prastel", .get_string = decode_prastel_get_string}};
static const SubGhzDecodeRegistry decode_prastel_registry = {
    .items = decode_prastel_items, .size = 1};
static const FlipperAppPluginDescriptor decode_prastel_descriptor = {
    .appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION,
    .entry_point = &decode_prastel_registry};
const FlipperAppPluginDescriptor* decode_prastel_ep(void) { return &decode_prastel_descriptor; }
