/* decode_power_smart.fal : SD get_string for power_smart (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/power_smart.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define CHANNEL_PATTERN "%c%c%c%c%c%c"
#define CNT_TO_CHANNEL(dip)                                                             \
    (dip & 0x0001 ? '*' : '-'), (dip & 0x0002 ? '*' : '-'), (dip & 0x0004 ? '*' : '-'), \
        (dip & 0x0008 ? '*' : '-'), (dip & 0x0010 ? '*' : '-'), (dip & 0x0020 ? '*' : '-')

struct SubGhzProtocolDecoderPowerSmart {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    ManchesterState manchester_saved_state;
    uint16_t header_count;
};

static void subghz_protocol_power_smart_remote_controller(SubGhzBlockGeneric* instance);
static const char* subghz_protocol_power_smart_get_name_button(uint8_t btn);

static void subghz_protocol_power_smart_remote_controller(SubGhzBlockGeneric* instance) {

    instance->btn = ((instance->data >> 54) & 0x02) | ((instance->data >> 40) & 0x1);
    instance->serial = ((instance->data >> 33) & 0x3FFF00) | ((instance->data >> 32) & 0xFF);
    instance->cnt = ((instance->data >> 49) & 0x3F);
}

static const char* subghz_protocol_power_smart_get_name_button(uint8_t btn) {
    btn &= 0x3;
    const char* name_btn[0x4] = {"Unknown", "Down", "Up", "Stop"};
    return name_btn[btn];
}

static void power_smart_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderPowerSmart* instance = input->decoder;

    subghz_protocol_power_smart_remote_controller(&instance->generic);

    subghz_block_generic_global.btn_is_available = false;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 2;

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "Key:0x%lX%08lX\r\n"
        "Sn:0x%07lX \r\n"
        "Btn:%s\r\n"
        "Channel:" CHANNEL_PATTERN "\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data >> 32),
        (uint32_t)(instance->generic.data & 0xFFFFFFFF),
        instance->generic.serial,
        subghz_protocol_power_smart_get_name_button(instance->generic.btn),
        CNT_TO_CHANNEL(instance->generic.cnt));
}

static const SubGhzDecodeEntry power_smart_items[] = {{.name = "Power Smart", .get_string = power_smart_decode}};
static const SubGhzDecodeRegistry power_smart_registry = {.items = power_smart_items, .size = 1};
static const FlipperAppPluginDescriptor decode_power_smart_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &power_smart_registry};
const FlipperAppPluginDescriptor* decode_power_smart_ep(void) { return &decode_power_smart_descriptor; }
