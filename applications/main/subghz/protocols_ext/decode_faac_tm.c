

#include "subghz_decode_plugin_desc.h"
#include <lib/subghz/blocks/generic.h>
#include <furi.h>

#define DIP_PATTERN "%c%c%c%c%c%c%c%c%c%c%c%c"
#define DATA_TO_DIP(dip)                                                                 \
    (dip & 0x800 ? '1' : '0'), (dip & 0x400 ? '1' : '0'), (dip & 0x200 ? '1' : '0'),     \
        (dip & 0x100 ? '1' : '0'), (dip & 0x080 ? '1' : '0'), (dip & 0x040 ? '1' : '0'), \
        (dip & 0x020 ? '1' : '0'), (dip & 0x010 ? '1' : '0'), (dip & 0x008 ? '1' : '0'), \
        (dip & 0x004 ? '1' : '0'), (dip & 0x002 ? '1' : '0'), (dip & 0x001 ? '1' : '0')

static void decode_faac_tm_get_string(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->generic) return;
    SubGhzBlockGeneric* generic = input->generic;
    uint32_t te = (input->extra && input->extra_count > 0) ? (uint32_t)input->extra[0] : 0;
    uint32_t data = (uint32_t)(generic->data & 0xFFF);
    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%03lX    Te:%luus\r\n"
        "DIP:" DIP_PATTERN "\r\n",
        generic->protocol_name,
        generic->data_count_bit,
        (unsigned long)data,
        (unsigned long)te,
        DATA_TO_DIP(data));
}

static const SubGhzDecodeEntry decode_faac_tm_items[] = {
    {.name = "Faac TM", .get_string = decode_faac_tm_get_string},
};
static const SubGhzDecodeRegistry decode_faac_tm_registry = {
    .items = decode_faac_tm_items,
    .size = 1,
};
static const FlipperAppPluginDescriptor decode_faac_tm_descriptor = {
    .appid = SUBGHZ_DECODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION,
    .entry_point = &decode_faac_tm_registry,
};
const FlipperAppPluginDescriptor* decode_faac_tm_ep(void) {
    return &decode_faac_tm_descriptor;
}
