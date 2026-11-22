

#include "subghz_decode_plugin_desc.h"
#include <lib/subghz/blocks/generic.h>
#include <furi.h>

static const char* dl_button_name(uint8_t code) {
    switch(code) {
    case 0x40:
        return "Btn0";
    case 0x80:
        return "Btn1";
    case 0x20:
        return "Btn3";
    case 0x60:
        return "Btn5";
    default:
        return "Other";
    }
}

static void decode_datsun_lada_get_string(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->generic) return;
    SubGhzBlockGeneric* generic = input->generic;
    uint8_t crc_valid = (input->extra && input->extra_count > 0) ? (uint8_t)input->extra[0] : 0;
    uint8_t btn = (uint8_t)generic->btn;
    furi_string_cat_printf(
        output,
        "%s 88bit\r\n"
        "Ser:0x%08lX\r\n"
        "Btn:0x%02X %s\r\n",
        generic->protocol_name,
        (unsigned long)generic->serial,
        btn,
        dl_button_name(btn));
    if(crc_valid)
        furi_string_cat_printf(output, "Cnt:%04lX\r\n", (unsigned long)(generic->cnt & 0xFFFF));
    else
        furi_string_cat_printf(output, "Check failed\r\n");
}

static const SubGhzDecodeEntry decode_datsun_lada_items[] = {
    {.name = "Datsun Lada", .get_string = decode_datsun_lada_get_string},
};
static const SubGhzDecodeRegistry decode_datsun_lada_registry = {
    .items = decode_datsun_lada_items,
    .size = 1,
};
static const FlipperAppPluginDescriptor decode_datsun_lada_descriptor = {
    .appid = SUBGHZ_DECODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION,
    .entry_point = &decode_datsun_lada_registry,
};
const FlipperAppPluginDescriptor* decode_datsun_lada_ep(void) {
    return &decode_datsun_lada_descriptor;
}
