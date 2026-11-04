/* decode_x10.fal : SD get_string for x10 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/x10.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderX10 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static void subghz_protocol_x10_check_remote_controller(SubGhzBlockGeneric* instance);

static void subghz_protocol_x10_check_remote_controller(SubGhzBlockGeneric* instance) {
    instance->serial = (instance->data & 0xF0000000) >> (24+4);
    instance->btn = (((instance->data & 0x07000000) >> 24) | ((instance->data & 0xF800) >> 8));
}

static const char* CHANNEL_LETTERS = "MNOPCDABEFGHKLIJ";

static void x10_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderX10* instance = input->decoder;

    subghz_protocol_x10_check_remote_controller(&instance->generic);

    char code_channel = CHANNEL_LETTERS[(instance->generic.serial & 0x0F)];

    uint32_t code_button = 1 + (
        ((instance->generic.btn&0x10) >> 4) |
        ((instance->generic.btn&0x8) >> 2) |
        ((instance->generic.btn&0x40)>>4) |
        ((instance->generic.btn&4)<<1));

    char* code_action = (instance->generic.btn & 0x20) == 0x20 ? "Off" : "On";
    if (instance->generic.btn == 0x98) {
        code_button = 0;
        code_action = "Dim";
    } else if (instance->generic.btn == 0x88) {
        code_button = 0;
        code_action = "Bright";
    }

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Channel:%c \r\n"
        "Button:%ld %s\r\n\r\n"
        "Key:%lX%08lX\r\n"
        "Sn:%07lX Btn:%X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        code_channel,
        code_button,
        code_action,
        (uint32_t)(instance->generic.data >> 32),
        (uint32_t)instance->generic.data,
        instance->generic.serial,
        instance->generic.btn);
}

static const SubGhzDecodeEntry x10_items[] = {{.name = "X10", .get_string = x10_decode}};
static const SubGhzDecodeRegistry x10_registry = {.items = x10_items, .size = 1};
static const FlipperAppPluginDescriptor decode_x10_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &x10_registry};
const FlipperAppPluginDescriptor* decode_x10_ep(void) { return &decode_x10_descriptor; }
