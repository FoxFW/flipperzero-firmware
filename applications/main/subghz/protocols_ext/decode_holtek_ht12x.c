/* decode_holtek_ht12x.fal : SD get_string for holtek_ht12x (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/holtek_ht12x.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define DIP_PATTERN "%c%c%c%c%c%c%c%c"
#define CNT_TO_DIP(dip)                                                                     \
    (dip & 0x0080 ? '0' : '1'), (dip & 0x0040 ? '0' : '1'), (dip & 0x0020 ? '0' : '1'),     \
        (dip & 0x0010 ? '0' : '1'), (dip & 0x0008 ? '0' : '1'), (dip & 0x0004 ? '0' : '1'), \
        (dip & 0x0002 ? '0' : '1'), (dip & 0x0001 ? '0' : '1')

struct SubGhzProtocolDecoderHoltek_HT12X {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t te;
    uint32_t last_data;
};

static void subghz_protocol_holtek_th12x_check_remote_controller(SubGhzBlockGeneric* instance);
static void subghz_protocol_holtek_th12x_event_serialize(uint8_t event, FuriString* output);

static void subghz_protocol_holtek_th12x_check_remote_controller(SubGhzBlockGeneric* instance) {
    instance->btn = instance->data & 0x0F;
    instance->cnt = (instance->data >> 4) & 0xFF;
}

static void subghz_protocol_holtek_th12x_event_serialize(uint8_t event, FuriString* output) {
    furi_string_cat_printf(
        output,
        "%s%s%s%s\r\n",
        (((event >> 3) & 0x1) == 0x0 ? "B1 " : ""),
        (((event >> 2) & 0x1) == 0x0 ? "B2 " : ""),
        (((event >> 1) & 0x1) == 0x0 ? "B3 " : ""),
        (((event >> 0) & 0x1) == 0x0 ? "B4 " : ""));
}

static void holtek_ht12x_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderHoltek_HT12X* instance = input->decoder;

    subghz_protocol_holtek_th12x_check_remote_controller(&instance->generic);

    subghz_block_generic_global.btn_is_available = false;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 4;

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "Key:0x%03lX\r\n"
        "Btn: ",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data & 0xFFF));
    subghz_protocol_holtek_th12x_event_serialize(instance->generic.btn, output);
    furi_string_cat_printf(
        output,
        "DIP:" DIP_PATTERN "\r\n"
        "Te:%luus\r\n",
        CNT_TO_DIP(instance->generic.cnt),
        instance->te);
}

static const SubGhzDecodeEntry holtek_ht12x_items[] = {{.name = "Holtek_HT12X", .get_string = holtek_ht12x_decode}};
static const SubGhzDecodeRegistry holtek_ht12x_registry = {.items = holtek_ht12x_items, .size = 1};
static const FlipperAppPluginDescriptor decode_holtek_ht12x_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &holtek_ht12x_registry};
const FlipperAppPluginDescriptor* decode_holtek_ht12x_ep(void) { return &decode_holtek_ht12x_descriptor; }
