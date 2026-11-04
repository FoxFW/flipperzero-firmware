/* decode_gangqi.fal : SD get_string for gangqi (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/gangqi.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>

struct SubGhzProtocolDecoderGangQi {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static void subghz_protocol_gangqi_remote_controller(SubGhzBlockGeneric* instance);
static const char* subghz_protocol_gangqi_get_button_name(uint8_t btn);

static void subghz_protocol_gangqi_remote_controller(SubGhzBlockGeneric* instance) {
    instance->btn = (instance->data >> 10) & 0xF;
    instance->serial = (instance->data & 0xFFFFF0000) >> 16;

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->btn);
    }
    subghz_custom_btn_set_max(3);

}

static const char* subghz_protocol_gangqi_get_button_name(uint8_t btn) {
    const char* name_btn[16] = {
        "Unknown",
        "Exit settings",
        "Volume setting",
        "0x3",
        "Vibro sens. setting",
        "Settings mode",
        "Ringtone setting",
        "Ring",
        "0x8",
        "0x9",
        "0xA",
        "Alarm",
        "0xC",
        "Arm",
        "Disarm",
        "0xF"};
    return btn <= 0xf ? name_btn[btn] : name_btn[0];
}

static void gangqi_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderGangQi* instance = input->decoder;


    subghz_protocol_gangqi_remote_controller(&instance->generic);

    uint16_t serial = (uint16_t)((instance->generic.data >> 18) & 0xFFFF);
    uint8_t const_and_button = (uint8_t)(0xD0 | instance->generic.btn);
    uint8_t serial_high = (uint8_t)(serial >> 8);
    uint8_t serial_low = (uint8_t)(serial & 0xFF);

    uint8_t sum_type1 = (uint8_t)(0xC8 - serial_high - serial_low - const_and_button);
    uint8_t sum_type2 = (uint8_t)(0x02 + serial_high + serial_low + const_and_button);

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 4;

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "Key: 0x%X%08lX\r\n"
        "Serial: 0x%05lX\r\n"
        "Sum: 0x%02X   Sum2: 0x%02X\r\n"
        "Btn: 0x%01X - %s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint8_t)(instance->generic.data >> 32),
        (uint32_t)(instance->generic.data & 0xFFFFFFFF),
        instance->generic.serial,
        sum_type1,
        sum_type2,
        instance->generic.btn,
        subghz_protocol_gangqi_get_button_name(instance->generic.btn));
}

static const SubGhzDecodeEntry gangqi_items[] = {{.name = "GangQi", .get_string = gangqi_decode}};
static const SubGhzDecodeRegistry gangqi_registry = {.items = gangqi_items, .size = 1};
static const FlipperAppPluginDescriptor decode_gangqi_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &gangqi_registry};
const FlipperAppPluginDescriptor* decode_gangqi_ep(void) { return &decode_gangqi_descriptor; }
