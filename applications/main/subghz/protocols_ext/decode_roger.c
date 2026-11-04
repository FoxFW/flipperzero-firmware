/* decode_roger.fal : SD get_string for roger (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/roger.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>

struct SubGhzProtocolDecoderRoger {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static void subghz_protocol_roger_check_remote_controller(SubGhzBlockGeneric* instance);

static void subghz_protocol_roger_check_remote_controller(SubGhzBlockGeneric* instance) {

    instance->serial = instance->data >> 12;
    instance->btn = (instance->data >> 8) & 0xF;

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->btn);
    }
    subghz_custom_btn_set_max(3);
}

static void roger_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderRoger* instance = input->decoder;


    subghz_protocol_roger_check_remote_controller(&instance->generic);

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 4;

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "Key: 0x%07lX\r\n"
        "Serial: 0x%04lX\r\n"
        "End: 0x%02lX\r\n"
        "Btn: %01X",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data & 0xFFFFFFF),
        instance->generic.serial,
        (uint32_t)(instance->generic.data & 0xFF),
        instance->generic.btn);
}

static const SubGhzDecodeEntry roger_items[] = {{.name = "Roger", .get_string = roger_decode}};
static const SubGhzDecodeRegistry roger_registry = {.items = roger_items, .size = 1};
static const FlipperAppPluginDescriptor decode_roger_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &roger_registry};
const FlipperAppPluginDescriptor* decode_roger_ep(void) { return &decode_roger_descriptor; }
