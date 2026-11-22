

#include "subghz_decode_plugin_desc.h"
#include <lib/subghz/blocks/generic.h>
#include <furi.h>

static const char* om_event_name(uint8_t e) {
    switch(e) {
    case 26:
        return "Lock/Unlock";
    case 3:
        return "Proximity";
    default:
        return "Other";
    }
}

static void decode_opel_mokka_get_string(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->generic) return;
    SubGhzBlockGeneric* generic = input->generic;
    uint8_t event = (uint8_t)generic->btn;
    furi_string_cat_printf(
        output,
        "%s 82bit\r\n"
        "KeyId:0x%03lX\r\n"
        "Event:%u %s\r\n",
        generic->protocol_name,
        (unsigned long)(generic->serial & 0x7FF),
        event,
        om_event_name(event));
}

static const SubGhzDecodeEntry decode_opel_mokka_items[] = {
    {.name = "Opel-Mokka", .get_string = decode_opel_mokka_get_string},
};
static const SubGhzDecodeRegistry decode_opel_mokka_registry = {
    .items = decode_opel_mokka_items,
    .size = 1,
};
static const FlipperAppPluginDescriptor decode_opel_mokka_descriptor = {
    .appid = SUBGHZ_DECODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION,
    .entry_point = &decode_opel_mokka_registry,
};
const FlipperAppPluginDescriptor* decode_opel_mokka_ep(void) {
    return &decode_opel_mokka_descriptor;
}
