/* decode_magellan.fal : SD get_string for magellan (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/magellan.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderMagellan {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;
};

static void subghz_protocol_magellan_check_remote_controller(SubGhzBlockGeneric* instance);
static void subghz_protocol_magellan_get_event_serialize(uint8_t event, FuriString* output);

static void subghz_protocol_magellan_check_remote_controller(SubGhzBlockGeneric* instance) {

    uint64_t data_rev = subghz_protocol_blocks_reverse_key(instance->data >> 8, 24);
    instance->serial = data_rev & 0xFFFF;
    instance->btn = (data_rev >> 16) & 0xFF;
}

static void subghz_protocol_magellan_get_event_serialize(uint8_t event, FuriString* output) {
    const char* event_type;
    const char* event_subtype;

    switch((event >> 4) & 0x0F) {
    case 0x00:
        event_type = "Nothing";
        break;
    case 0x01:
        event_type = "Door";
        break;
    case 0x02:
        event_type = "Motion";
        break;
    case 0x03:
        event_type = "Smoke Alarm";
        break;
    case 0x04:
        event_type = "REM1";
        break;
    case 0x05:
        event_type = "REM1";
        event_subtype = "Off1";
        furi_string_cat_printf(output, "%s - %s", event_type, event_subtype);
        return;
    case 0x06:
        event_type = "REM2";
        event_subtype = "Off1";
        furi_string_cat_printf(output, "%s - %s", event_type, event_subtype);
        return;
    default:
        event_type = "Unknown";
        break;
    }

    switch(event & 0x0F) {
    case 0x00:
        event_subtype = (((event >> 4) & 0x0F) > 0x03) ? "Arm1" : "Sealed";
        break;
    case 0x01:
        event_subtype = (((event >> 4) & 0x0F) > 0x03) ? "Btn1" : "Alarm";
        break;
    case 0x02:
        event_subtype = (((event >> 4) & 0x0F) > 0x03) ? "Btn2" : "Tamper";
        break;
    case 0x03:
        event_subtype = (((event >> 4) & 0x0F) > 0x03) ? "Btn3" : "Alarm + Tamper";
        break;
    case 0x08:
        event_subtype = "Reset";
        break;
    case 0x09:
        event_subtype = "LowBatt";
        break;
    case 0x0A:
        event_subtype = "BattOk";
        break;
    case 0x0B:
        event_subtype = "Learn";
        break;
    default:
        event_subtype = "Unknown";
        break;
    }

    furi_string_cat_printf(output, "%s - %s", event_type, event_subtype);
}

static void magellan_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderMagellan* instance = input->decoder;

    subghz_protocol_magellan_check_remote_controller(&instance->generic);

    subghz_block_generic_global.btn_is_available = false;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 8;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%08lX\r\n"
        "Sn:%03ld%03ld, Event:0x%02X\r\n"
        "Stat:",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data & 0xFFFFFFFF),
        (instance->generic.serial >> 8) & 0xFF,
        instance->generic.serial & 0xFF,
        instance->generic.btn);

    subghz_protocol_magellan_get_event_serialize(instance->generic.btn, output);
}

static const SubGhzDecodeEntry magellan_items[] = {{.name = "Magellan", .get_string = magellan_decode}};
static const SubGhzDecodeRegistry magellan_registry = {.items = magellan_items, .size = 1};
static const FlipperAppPluginDescriptor decode_magellan_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &magellan_registry};
const FlipperAppPluginDescriptor* decode_magellan_ep(void) { return &decode_magellan_descriptor; }
