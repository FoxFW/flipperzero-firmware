/* decode_marantec24.fal : SD get_string for marantec24 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/marantec24.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderMarantec24 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static void subghz_protocol_marantec24_check_remote_controller(SubGhzBlockGeneric* instance);

static void subghz_protocol_marantec24_check_remote_controller(SubGhzBlockGeneric* instance) {
    instance->serial = instance->data >> 4;
    instance->btn = instance->data & 0xF;
}

static void marantec24_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderMarantec24* instance = input->decoder;


    subghz_protocol_marantec24_check_remote_controller(&instance->generic);

    subghz_block_generic_global.btn_is_available = false;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 4;

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "Key: 0x%06lX\r\n"
        "Serial: 0x%05lX\r\n"
        "Btn: %01X",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data & 0xFFFFFF),
        instance->generic.serial,
        instance->generic.btn);
}

static const SubGhzDecodeEntry marantec24_items[] = {{.name = "Marantec24", .get_string = marantec24_decode}};
static const SubGhzDecodeRegistry marantec24_registry = {.items = marantec24_items, .size = 1};
static const FlipperAppPluginDescriptor decode_marantec24_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &marantec24_registry};
const FlipperAppPluginDescriptor* decode_marantec24_ep(void) { return &decode_marantec24_descriptor; }
