/* decode_ansonic.fal : SD get_string for ansonic (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/ansonic.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define DIP_PATTERN "%c%c%c%c%c%c%c%c%c%c"
#define CNT_TO_DIP(dip)                                                                     \
    (dip & 0x0800 ? '1' : '0'), (dip & 0x0400 ? '1' : '0'), (dip & 0x0200 ? '1' : '0'),     \
        (dip & 0x0100 ? '1' : '0'), (dip & 0x0080 ? '1' : '0'), (dip & 0x0040 ? '1' : '0'), \
        (dip & 0x0020 ? '1' : '0'), (dip & 0x0010 ? '1' : '0'), (dip & 0x0001 ? '1' : '0'), \
        (dip & 0x0008 ? '1' : '0')

struct SubGhzProtocolDecoderAnsonic {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static void subghz_protocol_ansonic_check_remote_controller(SubGhzBlockGeneric* instance);

static void subghz_protocol_ansonic_check_remote_controller(SubGhzBlockGeneric* instance) {

    instance->cnt = instance->data & 0xFFF;
    instance->btn = ((instance->data >> 1) & 0x3);
}

static void ansonic_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderAnsonic* instance = input->decoder;

    subghz_protocol_ansonic_check_remote_controller(&instance->generic);

    subghz_block_generic_global.btn_is_available = false;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 2;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%03lX\r\n"
        "Btn:%X\r\n"
        "DIP:" DIP_PATTERN "\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data & 0xFFFFFFFF),
        instance->generic.btn,
        CNT_TO_DIP(instance->generic.cnt));
}

static const SubGhzDecodeEntry ansonic_items[] = {{.name = "Ansonic", .get_string = ansonic_decode}};
static const SubGhzDecodeRegistry ansonic_registry = {.items = ansonic_items, .size = 1};
static const FlipperAppPluginDescriptor decode_ansonic_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &ansonic_registry};
const FlipperAppPluginDescriptor* decode_ansonic_ep(void) { return &decode_ansonic_descriptor; }
