/* decode_treadmill37.fal : SD get_string for treadmill37 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/treadmill37.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderTreadmill37 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static void subghz_protocol_treadmill37_check_remote_controller(SubGhzBlockGeneric* instance);

static void subghz_protocol_treadmill37_check_remote_controller(SubGhzBlockGeneric* instance) {
    instance->serial = instance->data >> 17;
    instance->cnt = (instance->data >> 1) & 0xFFFF;
}

static void treadmill37_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderTreadmill37* instance = input->decoder;


    subghz_protocol_treadmill37_check_remote_controller(&instance->generic);

    uint64_t code_found_reverse = subghz_protocol_blocks_reverse_key(
        instance->generic.data, instance->generic.data_count_bit);

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "Key: 0x%08llX\r\n"
        "Yek: 0x%08llX\r\n"
        "Serial: 0x%06lX\r\n"
        "Btn: %04lX",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint64_t)(instance->generic.data & 0xFFFFFFFFFF),
        (code_found_reverse & 0xFFFFFFFFFF),
        instance->generic.serial,
        instance->generic.cnt);
}

static const SubGhzDecodeEntry treadmill37_items[] = {{.name = "Treadmill37", .get_string = treadmill37_decode}};
static const SubGhzDecodeRegistry treadmill37_registry = {.items = treadmill37_items, .size = 1};
static const FlipperAppPluginDescriptor decode_treadmill37_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &treadmill37_registry};
const FlipperAppPluginDescriptor* decode_treadmill37_ep(void) { return &decode_treadmill37_descriptor; }
