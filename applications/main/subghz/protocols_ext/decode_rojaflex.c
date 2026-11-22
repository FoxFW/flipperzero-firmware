

#include "subghz_decode_plugin_desc.h"
#include <lib/subghz/blocks/generic.h>
#include <furi.h>

static const char* rojaflex_model_name(uint8_t model) {
    switch(model) {
    case 0:
        return "Remote";
    case 1:
        return "Shutter";
    case 2:
        return "Bridge";
    default:
        return "Other";
    }
}

static const char* rojaflex_cmd_name(uint8_t code) {
    switch(code) {
    case 0x0a:
        return "Stop";
    case 0x1a:
        return "Up";
    case 0x8a:
        return "Down";
    case 0x9a:
        return "Save/Unsave";
    case 0xda:
        return "Go saved";
    case 0xea:
        return "Req Status";
    case 0x85:
        return "Pos 0%";
    case 0x95:
        return "Pos 20%";
    case 0xA5:
        return "Pos 40%";
    case 0xB5:
        return "Pos 60%";
    case 0xC5:
        return "Pos 80%";
    case 0xD5:
        return "Pos 100%";
    default:
        return "unknown";
    }
}

static void decode_rojaflex_get_string(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->generic) return;
    SubGhzBlockGeneric* generic = input->generic;
    uint8_t channel = (input->extra && input->extra_count > 0) ? (uint8_t)input->extra[0] : 0;
    uint8_t model = (input->extra && input->extra_count > 1) ? (uint8_t)input->extra[1] : 3;
    uint8_t crc_ok = (input->extra && input->extra_count > 2) ? (uint8_t)input->extra[2] : 0;
    uint8_t cmd = (uint8_t)generic->btn;
    furi_string_cat_printf(
        output,
        "%s %s %dbit\r\n"
        "Id:0x%07lX Ch:%u\r\n"
        "Cmd:0x%02X %s\r\n"
        "%s",
        generic->protocol_name,
        rojaflex_model_name(model),
        generic->data_count_bit,
        (unsigned long)generic->serial,
        channel,
        cmd,
        rojaflex_cmd_name(cmd),
        crc_ok ? "" : "(unchecked)\r\n");
}

static const SubGhzDecodeEntry decode_rojaflex_items[] = {
    {.name = "RojaFlex", .get_string = decode_rojaflex_get_string},
};
static const SubGhzDecodeRegistry decode_rojaflex_registry = {
    .items = decode_rojaflex_items,
    .size = 1,
};
static const FlipperAppPluginDescriptor decode_rojaflex_descriptor = {
    .appid = SUBGHZ_DECODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION,
    .entry_point = &decode_rojaflex_registry,
};
const FlipperAppPluginDescriptor* decode_rojaflex_ep(void) {
    return &decode_rojaflex_descriptor;
}
