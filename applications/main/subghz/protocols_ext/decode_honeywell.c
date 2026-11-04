/* decode_honeywell.fal : SD get_string for honeywell (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/honeywell.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderHoneywell {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockGeneric generic;
    SubGhzBlockDecoder decoder;
    ManchesterState manchester_saved_state;
};

static void honeywell_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderHoneywell* instance = input->decoder;


    uint32_t code_found_hi = instance->generic.data >> 32;
    uint32_t code_found_lo = instance->generic.data & 0x00000000ffffffff;

    instance->generic.serial = (instance->generic.data >> 24) & 0xFFFFF;
    uint8_t sensor_status = (instance->generic.data >> 16) & 0xFF;

    uint8_t channel = (instance->generic.data >> 44) & 0xF;
    uint8_t contact = (sensor_status & 0x80) >> 7;
    uint8_t tamper = (sensor_status & 0x40) >> 6;
    uint8_t reed = (sensor_status & 0x20) >> 5;
    uint8_t alarm = (sensor_status & 0x10) >> 4;
    uint8_t battery_low = (sensor_status & 0x08) >> 3;
    uint8_t heartbeat = (sensor_status & 0x04) >> 2;

    furi_string_cat_printf(
        output,
        "%s\r\n%dbit  "
        "Sn:%07lu  Ch:%u\r\n"
        "LowBat:%d  HB: %d  Cont: %s\r\n"
        "Key:%08lX%08lX\r\n"
        "State: L1:%u  L2:%u  L3:%u  L4:%u",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        instance->generic.serial,
        channel,
        battery_low,
        heartbeat,
        contact ? "open" : "closed",
        code_found_hi,
        code_found_lo,
        contact,
        reed,
        alarm,
        tamper);
}

static const SubGhzDecodeEntry honeywell_items[] = {{.name = "Honeywell Sec", .get_string = honeywell_decode}};
static const SubGhzDecodeRegistry honeywell_registry = {.items = honeywell_items, .size = 1};
static const FlipperAppPluginDescriptor decode_honeywell_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &honeywell_registry};
const FlipperAppPluginDescriptor* decode_honeywell_ep(void) { return &decode_honeywell_descriptor; }
