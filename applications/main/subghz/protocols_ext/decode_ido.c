/* decode_ido.fal : SD get_string for ido (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/ido.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderIDo {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static void subghz_protocol_ido_check_remote_controller(SubGhzBlockGeneric* instance);

static void subghz_protocol_ido_check_remote_controller(SubGhzBlockGeneric* instance) {
    uint64_t code_found_reverse =
        subghz_protocol_blocks_reverse_key(instance->data, instance->data_count_bit);
    uint32_t code_fix = code_found_reverse & 0xFFFFFF;

    instance->serial = code_fix & 0xFFFFF;
    instance->btn = (code_fix >> 20) & 0x0F;
}

static void ido_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderIDo* instance = input->decoder;


    subghz_protocol_ido_check_remote_controller(&instance->generic);
    uint64_t code_found_reverse = subghz_protocol_blocks_reverse_key(
        instance->generic.data, instance->generic.data_count_bit);
    uint32_t code_fix = code_found_reverse & 0xFFFFFF;
    uint32_t code_hop = (code_found_reverse >> 24) & 0xFFFFFF;

    subghz_block_generic_global.btn_is_available = false;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 4;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%lX%08lX\r\n"
        "Fix:%06lX \r\n"
        "Hop:%06lX \r\n"
        "Sn:%05lX Btn:%X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data >> 32),
        (uint32_t)instance->generic.data,
        code_fix,
        code_hop,
        instance->generic.serial,
        instance->generic.btn);
}

static const SubGhzDecodeEntry ido_items[] = {{.name = "iDo 117/111", .get_string = ido_decode}};
static const SubGhzDecodeRegistry ido_registry = {.items = ido_items, .size = 1};
static const FlipperAppPluginDescriptor decode_ido_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &ido_registry};
const FlipperAppPluginDescriptor* decode_ido_ep(void) { return &decode_ido_descriptor; }
