
#include "subghz_decode_plugin_desc.h"
#include <lib/subghz/blocks/generic.h>
#include <furi.h>
static const char* subaru_btn_name(uint8_t btn) {
    static const char* const names[4] = {"Lock", "Unlock", "Trunk", "Panic"};
    return names[btn & 0x03];
}
static void decode_subaru_get_string(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->generic) return;
    SubGhzBlockGeneric* g = input->generic;
    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%016llX\r\n"
        "Sn:%06lX Btn:%02X [%s]\r\nCnt:%04lX\r\n",
        g->protocol_name, g->data_count_bit, (unsigned long long)g->data,
        (unsigned long)g->serial, g->btn, subaru_btn_name(g->btn), (unsigned long)g->cnt);
}
static const SubGhzDecodeEntry decode_subaru_items[] = {
    {.name = "SUBARU", .get_string = decode_subaru_get_string}};
static const SubGhzDecodeRegistry decode_subaru_registry = {.items = decode_subaru_items, .size = 1};
static const FlipperAppPluginDescriptor decode_subaru_descriptor = {
    .appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION,
    .entry_point = &decode_subaru_registry};
const FlipperAppPluginDescriptor* decode_subaru_ep(void) { return &decode_subaru_descriptor; }
