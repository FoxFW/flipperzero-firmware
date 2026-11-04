/* decode_revers_rb2.fal : SD get_string for revers_rb2 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/revers_rb2.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderRevers_RB2 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    ManchesterState manchester_saved_state;
    uint16_t header_count;
};

static void subghz_protocol_revers_rb2_remote_controller(SubGhzBlockGeneric* instance);

static void subghz_protocol_revers_rb2_remote_controller(SubGhzBlockGeneric* instance) {

    instance->serial = (((instance->data << 16) >> 16) >> 10);
}

static void revers_rb2_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderRevers_RB2* instance = input->decoder;

    subghz_protocol_revers_rb2_remote_controller(&instance->generic);

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "Key:%lX%08lX\r\n"
        "Sn:0x%08lX \r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data >> 32),
        (uint32_t)(instance->generic.data & 0xFFFFFFFF),
        instance->generic.serial);
}

static const SubGhzDecodeEntry revers_rb2_items[] = {{.name = "Revers_RB2", .get_string = revers_rb2_decode}};
static const SubGhzDecodeRegistry revers_rb2_registry = {.items = revers_rb2_items, .size = 1};
static const FlipperAppPluginDescriptor decode_revers_rb2_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &revers_rb2_registry};
const FlipperAppPluginDescriptor* decode_revers_rb2_ep(void) { return &decode_revers_rb2_descriptor; }
