/* decode_came_atomo.fal : SD get_string for came_atomo (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/came_atomo.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>

struct SubGhzProtocolDecoderCameAtomo {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_saved_state;
};

static void came_atomo_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderCameAtomo* instance = input->decoder;

    /* Atomo decrypt done internally */
    uint32_t code_found_hi = instance->generic.data >> 32;
    uint32_t code_found_lo = instance->generic.data & 0x00000000ffffffff;

    subghz_block_generic_global.cnt_is_available = true;
    subghz_block_generic_global.cnt_length_bit = 16;
    subghz_block_generic_global.current_cnt = instance->generic.cnt;

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 4;

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "Key:%08lX%08lX\r\n"
        "Sn:0x%08lX       Btn:%01X\r\n"
        "Cnt:%04lX\r\n"
        "Btn_Cnt:0x%02X",

        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        code_found_hi,
        code_found_lo,
        instance->generic.serial,
        instance->generic.btn,
        instance->generic.cnt,
        instance->generic.cnt_2);
}

static const SubGhzDecodeEntry came_atomo_items[] = {{.name = "CAME Atomo", .get_string = came_atomo_decode}};
static const SubGhzDecodeRegistry came_atomo_registry = {.items = came_atomo_items, .size = 1};
static const FlipperAppPluginDescriptor decode_came_atomo_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &came_atomo_registry};
const FlipperAppPluginDescriptor* decode_came_atomo_ep(void) { return &decode_came_atomo_descriptor; }
