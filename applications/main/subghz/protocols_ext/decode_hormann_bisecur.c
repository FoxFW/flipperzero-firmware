
#include "subghz_decode_plugin_desc.h"
#include <lib/subghz/blocks/generic.h>
#include <furi.h>
static void decode_hormann_bisecur_get_string(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->generic) return;
    SubGhzBlockGeneric* g = input->generic;
    uint8_t crc = (input->extra && input->extra_count > 0) ? (uint8_t)input->extra[0] : 0;
    uint8_t type = (input->extra && input->extra_count > 1) ? (uint8_t)input->extra[1] : 0;
    bool valid = (input->extra && input->extra_count > 2) ? (bool)input->extra[2] : false;
    furi_string_cat_printf(
        output,
        "%s\r\n"
        "%dbit CRC:0x%02X %s\r\n"
        "Type:0x%02X Sn:0x%08lX\r\n"
        "Key:%016llX\r\n"
        "Key:%016llX\r\n",
        g->protocol_name, g->data_count_bit, crc, valid ? "OK" : "WRONG", type,
        (unsigned long)g->serial, (unsigned long long)g->data, (unsigned long long)g->data_2);
}
static const SubGhzDecodeEntry decode_hormann_bisecur_items[] = {
    {.name = "Hormann BiSecur", .get_string = decode_hormann_bisecur_get_string}};
static const SubGhzDecodeRegistry decode_hormann_bisecur_registry = {
    .items = decode_hormann_bisecur_items, .size = 1};
static const FlipperAppPluginDescriptor decode_hormann_bisecur_descriptor = {
    .appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION,
    .entry_point = &decode_hormann_bisecur_registry};
const FlipperAppPluginDescriptor* decode_hormann_bisecur_ep(void) { return &decode_hormann_bisecur_descriptor; }
