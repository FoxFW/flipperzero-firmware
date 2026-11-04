/* decode_jarolift.fal : SD get_string for jarolift (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/jarolift.h>
#include <core/log.h>
#include <lib/subghz/protocols/keeloq_common.h>
#include <lib/subghz/subghz_keystore.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>

struct SubGhzProtocolDecoderJarolift {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t header_count;
    SubGhzKeystore* keystore;
};

static const char* subghz_protocol_jarolift_get_button_name(uint8_t btn);

static const char* subghz_protocol_jarolift_get_button_name(uint8_t btn) {
    const char* btn_name;
    switch(btn) {
    case 0x1:
        btn_name = "Learn";
        break;
    case 0x2:
        btn_name = "Down";
        break;
    case 0x4:
        btn_name = "Stop";
        break;
    case 0x8:
        btn_name = "Up";
        break;
    default:
        btn_name = "Unkn";
        break;
    }
    return btn_name;
}

static void jarolift_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderJarolift* instance = input->decoder;

    /* KeeLoq decrypt done internally */

    subghz_block_generic_global.cnt_is_available = true;
    subghz_block_generic_global.cnt_length_bit = 16;
    subghz_block_generic_global.current_cnt = instance->generic.cnt;

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 4;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%0llX\r\n"
        "Sn:%07lX  Btn:%01X - %s\r\n"
        "Cnt:%04lX Group:%04lX\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        instance->generic.data,
        instance->generic.serial,
        instance->generic.btn,
        subghz_protocol_jarolift_get_button_name(instance->generic.btn),
        instance->generic.cnt,
        instance->generic.seed);
}

static const SubGhzDecodeEntry jarolift_items[] = {{.name = "Jarolift", .get_string = jarolift_decode}};
static const SubGhzDecodeRegistry jarolift_registry = {.items = jarolift_items, .size = 1};
static const FlipperAppPluginDescriptor decode_jarolift_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &jarolift_registry};
const FlipperAppPluginDescriptor* decode_jarolift_ep(void) { return &decode_jarolift_descriptor; }
