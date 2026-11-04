/* decode_feron.fal : SD get_string for feron (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/feron.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderFeron {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static void subghz_protocol_feron_check_remote_controller(SubGhzBlockGeneric* instance);

static void subghz_protocol_feron_check_remote_controller(SubGhzBlockGeneric* instance) {
    instance->serial = instance->data >> 16;
}

static void feron_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderFeron* instance = input->decoder;


    subghz_protocol_feron_check_remote_controller(&instance->generic);

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "Key: 0x%08lX\r\n"
        "Serial: 0x%04lX\r\n"
        "Command: 0x%04lX\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data & 0xFFFFFFFF),
        instance->generic.serial,
        (uint32_t)(instance->generic.data & 0xFFFF));
}

static const SubGhzDecodeEntry feron_items[] = {{.name = "Feron", .get_string = feron_decode}};
static const SubGhzDecodeRegistry feron_registry = {.items = feron_items, .size = 1};
static const FlipperAppPluginDescriptor decode_feron_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &feron_registry};
const FlipperAppPluginDescriptor* decode_feron_ep(void) { return &decode_feron_descriptor; }
