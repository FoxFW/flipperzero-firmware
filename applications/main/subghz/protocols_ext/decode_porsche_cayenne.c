/* decode_porsche_cayenne.fal : SD get_string for porsche_cayenne (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/porsche_cayenne.h>
#include <lib/subghz/blocks/custom_btn_i.h>

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder        decoder;
    SubGhzBlockGeneric        generic;

    uint16_t sync_count;
    uint64_t raw_data;
    uint8_t  bit_count;
} SubGhzProtocolDecoderPorscheCayenne;

static uint8_t porsche_cayenne_btn_to_custom(uint8_t btn);

static uint8_t porsche_cayenne_btn_to_custom(uint8_t btn) {
    switch(btn) {
    case 0x01: return SUBGHZ_CUSTOM_BTN_UP;
    case 0x02: return SUBGHZ_CUSTOM_BTN_DOWN;
    case 0x04: return SUBGHZ_CUSTOM_BTN_LEFT;
    case 0x08: return SUBGHZ_CUSTOM_BTN_RIGHT;
    default: return SUBGHZ_CUSTOM_BTN_OK;
    }
}

static void porsche_cayenne_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderPorscheCayenne* instance = input->decoder;


    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(porsche_cayenne_btn_to_custom(instance->generic.btn));
    }
    subghz_custom_btn_set_max(4);

    uint8_t frame_type = (uint8_t)(instance->generic.data >> 56) & 0x07;
    const char* ft_name = "??";
    if(frame_type == 0b010) ft_name = "First";
    else if(frame_type == 0b001) ft_name = "Cont";
    else if(frame_type == 0b100) ft_name = "Final";

    furi_string_cat_printf(
        output,
        "%s 64bit\r\n"
        "Sn:%06lX Btn:%X\r\n"
        "Cnt:%04lX FT:%s\r\n"
        "Raw:%08lX%08lX\r\n",
        instance->generic.protocol_name,
        (unsigned long)(instance->generic.serial & 0xFFFFFF),
        (unsigned int)instance->generic.btn,
        (unsigned long)instance->generic.cnt,
        ft_name,
        (unsigned long)(instance->generic.data >> 32),
        (unsigned long)(instance->generic.data & 0xFFFFFFFF));
}

static const SubGhzDecodeEntry porsche_cayenne_items[] = {{.name = "Porsche AG", .get_string = porsche_cayenne_decode}};
static const SubGhzDecodeRegistry porsche_cayenne_registry = {.items = porsche_cayenne_items, .size = 1};
static const FlipperAppPluginDescriptor decode_porsche_cayenne_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &porsche_cayenne_registry};
const FlipperAppPluginDescriptor* decode_porsche_cayenne_ep(void) { return &decode_porsche_cayenne_descriptor; }
