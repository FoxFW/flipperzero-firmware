/* decode_doitrand.fal : SD get_string for doitrand (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/doitrand.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define DIP_PATTERN "%c%c%c%c%c%c%c%c%c%c"
#define CNT_TO_DIP(dip)                                                                     \
    (dip & 0x0001 ? '1' : '0'), (dip & 0x0100 ? '1' : '0'), (dip & 0x0080 ? '1' : '0'),     \
        (dip & 0x0040 ? '1' : '0'), (dip & 0x0020 ? '1' : '0'), (dip & 0x1000 ? '1' : '0'), \
        (dip & 0x0800 ? '1' : '0'), (dip & 0x0400 ? '1' : '0'), (dip & 0x0200 ? '1' : '0'), \
        (dip & 0x0002 ? '1' : '0')

struct SubGhzProtocolDecoderDoitrand {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static void subghz_protocol_doitrand_check_remote_controller(SubGhzBlockGeneric* instance);

static void subghz_protocol_doitrand_check_remote_controller(SubGhzBlockGeneric* instance) {

    instance->cnt = (instance->data >> 24) | ((instance->data >> 15) & 0x1);
    instance->btn = ((instance->data >> 18) & 0x3);
}

static void doitrand_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderDoitrand* instance = input->decoder;

    subghz_protocol_doitrand_check_remote_controller(&instance->generic);

    subghz_block_generic_global.btn_is_available = false;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 2;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%02lX%08lX\r\n"
        "Btn:%X\r\n"
        "DIP:" DIP_PATTERN "\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data >> 32) & 0xFFFFFFFF,
        (uint32_t)(instance->generic.data & 0xFFFFFFFF),
        instance->generic.btn,
        CNT_TO_DIP(instance->generic.cnt));
}

static const SubGhzDecodeEntry doitrand_items[] = {{.name = "Doitrand", .get_string = doitrand_decode}};
static const SubGhzDecodeRegistry doitrand_registry = {.items = doitrand_items, .size = 1};
static const FlipperAppPluginDescriptor decode_doitrand_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &doitrand_registry};
const FlipperAppPluginDescriptor* decode_doitrand_ep(void) { return &decode_doitrand_descriptor; }
