/* decode_honeywell_wdb.fal : SD get_string for honeywell_wdb (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/honeywell_wdb.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderHoneywell_WDB {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    const char* device_type;
    const char* alert;
    uint8_t secret_knock;
    uint8_t relay;
    uint8_t lowbat;
};

static void subghz_protocol_honeywell_wdb_check_remote_controller(
    SubGhzProtocolDecoderHoneywell_WDB* instance);

static void subghz_protocol_honeywell_wdb_check_remote_controller(
    SubGhzProtocolDecoderHoneywell_WDB* instance) {

    instance->generic.serial = (instance->generic.data >> 28) & 0xFFFFF;
    switch((instance->generic.data >> 20) & 0x3) {
    case 0x02:
        instance->device_type = "Doorbell";
        break;
    case 0x01:
        instance->device_type = "PIR-Motion";
        break;
    default:
        instance->device_type = "Unknown";
        break;
    }

    switch((instance->generic.data >> 16) & 0x3) {
    case 0x00:
        instance->alert = "Normal";
        break;
    case 0x01:
    case 0x02:
        instance->alert = "High";
        break;
    case 0x03:
        instance->alert = "Full";
        break;
    default:
        instance->alert = "Unknown";
        break;
    }

    instance->secret_knock = (uint8_t)((instance->generic.data >> 4) & 0x1);
    instance->relay = (uint8_t)((instance->generic.data >> 3) & 0x1);
    instance->lowbat = (uint8_t)((instance->generic.data >> 1) & 0x1);
}

static void honeywell_wdb_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderHoneywell_WDB* instance = input->decoder;

    subghz_protocol_honeywell_wdb_check_remote_controller(instance);

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%lX%08lX\r\n"
        "Sn:0x%05lX\r\n"
        "DT:%s  Al:%s\r\n"
        "SK:%01X R:%01X LBat:%01X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)((instance->generic.data >> 32) & 0xFFFFFFFF),
        (uint32_t)(instance->generic.data & 0xFFFFFFFF),
        instance->generic.serial,
        instance->device_type,
        instance->alert,
        instance->secret_knock,
        instance->relay,
        instance->lowbat);
}

static const SubGhzDecodeEntry honeywell_wdb_items[] = {{.name = "Honeywell", .get_string = honeywell_wdb_decode}};
static const SubGhzDecodeRegistry honeywell_wdb_registry = {.items = honeywell_wdb_items, .size = 1};
static const FlipperAppPluginDescriptor decode_honeywell_wdb_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &honeywell_wdb_registry};
const FlipperAppPluginDescriptor* decode_honeywell_wdb_ep(void) { return &decode_honeywell_wdb_descriptor; }
