/* decode_kinggates_stylo_4k.fal : SD get_string for kinggates_stylo_4k (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/kinggates_stylo_4k.h>
#include <lib/subghz/protocols/keeloq_common.h>
#include <lib/subghz/subghz_keystore.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>

struct SubGhzProtocolDecoderKingGates_stylo_4k {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t header_count;
    SubGhzKeystore* keystore;
};

static void kinggates_stylo_4k_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderKingGates_stylo_4k* instance = input->decoder;

    /* KeeLoq decrypt done internally */

    subghz_block_generic_global.cnt_is_available = true;
    subghz_block_generic_global.cnt_length_bit = 16;
    subghz_block_generic_global.current_cnt = instance->generic.cnt;

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 4;

    furi_string_cat_printf(
        output,
        "%s\r\n"
        "Key:0x%llX%07llX  %dbit\r\n"
        "Sn:0x%08lX  Btn:0x%01X\r\n"
        "Cnt:%04lX\r\n",
        instance->generic.protocol_name,
        instance->generic.data,
        instance->generic.data_2,
        instance->generic.data_count_bit,
        instance->generic.serial,
        instance->generic.btn,
        instance->generic.cnt);
}

static const SubGhzDecodeEntry kinggates_stylo_4k_items[] = {{.name = "KingGates Stylo4k", .get_string = kinggates_stylo_4k_decode}};
static const SubGhzDecodeRegistry kinggates_stylo_4k_registry = {.items = kinggates_stylo_4k_items, .size = 1};
static const FlipperAppPluginDescriptor decode_kinggates_stylo_4k_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &kinggates_stylo_4k_registry};
const FlipperAppPluginDescriptor* decode_kinggates_stylo_4k_ep(void) { return &decode_kinggates_stylo_4k_descriptor; }
