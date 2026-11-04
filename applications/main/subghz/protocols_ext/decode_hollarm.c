/* decode_hollarm.fal : SD get_string for hollarm (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/hollarm.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>

struct SubGhzProtocolDecoderHollarm {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static void subghz_protocol_hollarm_remote_controller(SubGhzBlockGeneric* instance);
static const char* subghz_protocol_hollarm_get_button_name(uint8_t btn);

static void subghz_protocol_hollarm_remote_controller(SubGhzBlockGeneric* instance) {
    instance->btn = (instance->data >> 8) & 0xF;
    instance->serial = (instance->data & 0xFFFFFFF0000) >> 16;

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->btn);
    }
    subghz_custom_btn_set_max(3);

}

static const char* subghz_protocol_hollarm_get_button_name(uint8_t btn) {
    const char* name_btn[16] = {
        "Unknown",
        "Disarm",
        "Arm",
        "0x3",
        "Ringtone/Alarm",
        "0x5",
        "0x6",
        "0x7",
        "Ring",
        "Settings mode",
        "Exit settings",
        "Vibro sens. setting",
        "Not used\n(in settings)",
        "Volume setting",
        "0xE",
        "0xF"};
    return btn <= 0xf ? name_btn[btn] : name_btn[0];
}

static void hollarm_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderHollarm* instance = input->decoder;


    subghz_protocol_hollarm_remote_controller(&instance->generic);

    uint8_t bytesum =
        ((instance->generic.data >> 32) & 0xFF) + ((instance->generic.data >> 24) & 0xFF) +
        ((instance->generic.data >> 16) & 0xFF) + ((instance->generic.data >> 8) & 0xFF);

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 4;

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "Key: 0x%02lX%08lX\r\n"
        "Serial: 0x%06lX  Sum: %02X\r\n"
        "Btn: 0x%01X - %s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data >> 32),
        (uint32_t)instance->generic.data,
        instance->generic.serial,
        bytesum,
        instance->generic.btn,
        subghz_protocol_hollarm_get_button_name(instance->generic.btn));
}

static const SubGhzDecodeEntry hollarm_items[] = {{.name = "Hollarm", .get_string = hollarm_decode}};
static const SubGhzDecodeRegistry hollarm_registry = {.items = hollarm_items, .size = 1};
static const FlipperAppPluginDescriptor decode_hollarm_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &hollarm_registry};
const FlipperAppPluginDescriptor* decode_hollarm_ep(void) { return &decode_hollarm_descriptor; }
