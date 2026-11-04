#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/bmw_cas3.h>
#include <lib/subghz/blocks/generic.h>

static void bmw_cas3_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->generic) return;
    const SubGhzBlockGeneric* generic = input->generic;
    furi_string_cat_printf(
        output,
        "%s %dbit\r\nKey:%08lX%08lX\r\n",
        generic->protocol_name,
        (int)generic->data_count_bit,
        (uint32_t)(generic->data >> 32),
        (uint32_t)(generic->data & 0xFFFFFFFF));
    if(generic->data_count_bit > 64) {
        furi_string_cat_printf(
            output,
            "Ext:%08lX%08lX\r\n",
            (uint32_t)(generic->data_2 >> 32),
            (uint32_t)(generic->data_2 & 0xFFFFFFFF));
    }
    furi_string_cat_printf(output, "AES encrypted\r\nSend replays this frame");
}

static const SubGhzDecodeEntry bmw_cas3_items[] = {
    {.name = BMW_CAS3_PROTOCOL_NAME, .get_string = bmw_cas3_decode}};
static const SubGhzDecodeRegistry bmw_cas3_registry = {.items = bmw_cas3_items, .size = 1};
static const FlipperAppPluginDescriptor decode_bmw_cas3_descriptor = {
    .appid = SUBGHZ_DECODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION,
    .entry_point = &bmw_cas3_registry};
const FlipperAppPluginDescriptor* decode_bmw_cas3_ep(void) {
    return &decode_bmw_cas3_descriptor;
}
