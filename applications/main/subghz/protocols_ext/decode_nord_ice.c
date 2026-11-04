/* decode_nord_ice.fal : SD get_string for nord_ice (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/nord_ice.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderNordIce {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static void subghz_protocol_nord_ice_check_remote_controller(SubGhzBlockGeneric* instance);

static void subghz_protocol_nord_ice_check_remote_controller(SubGhzBlockGeneric* instance) {
    instance->serial = ((instance->data >> 15) << 9) | (instance->data & 0x1FF);
    instance->btn = (instance->data >> 9) & 0x3F;
}

static void nord_ice_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderNordIce* instance = input->decoder;


    subghz_protocol_nord_ice_check_remote_controller(&instance->generic);

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "Key:0x%08lX\r\n"
        "Serial:0x%07lX\r\n"
        "Btn:%02X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)(instance->generic.data & 0xFFFFFFFF),
        (unsigned long)instance->generic.serial,
        instance->generic.btn);
}

static const SubGhzDecodeEntry nord_ice_items[] = {{.name = "Nord ICE", .get_string = nord_ice_decode}};
static const SubGhzDecodeRegistry nord_ice_registry = {.items = nord_ice_items, .size = 1};
static const FlipperAppPluginDescriptor decode_nord_ice_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &nord_ice_registry};
const FlipperAppPluginDescriptor* decode_nord_ice_ep(void) { return &decode_nord_ice_descriptor; }
