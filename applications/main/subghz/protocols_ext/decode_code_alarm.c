/* decode_code_alarm.fal : SD get_string for code_alarm (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/code_alarm.h>
#include <furi.h>
#include <string.h>
#include <lib/subghz/types.h>
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>
#include <flipper_format/flipper_format.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

typedef struct SubGhzProtocolDecoderCodeAlarm {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;
} SubGhzProtocolDecoderCodeAlarm;

static const char* code_alarm_button_name(uint8_t button);

static const char* code_alarm_button_name(uint8_t button) {
    // Order matches the rtl_433 button_map: first match wins for shared codes.
    switch(button) {
    case 0x7:
        return "Multiple";
    case 0x6:
    case 0x4:
        return "Lock";
    case 0x1:
    case 0x3:
        return "Panic";
    case 0x0:
        return "Start";
    case 0x5:
        return "Unlock";
    default:
        return "Unknown";
    }
}

static void code_alarm_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderCodeAlarm* instance = input->decoder;


    uint32_t data_hi = (uint32_t)(instance->generic.data >> 32);
    uint32_t data_lo = (uint32_t)(instance->generic.data & 0xFFFFFFFF);

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Data:%03lX%08lX\r\n"
        "Id:%06lX\r\n"
        "Btn:%X - %s",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)(data_hi & 0xFFF),
        (unsigned long)data_lo,
        (unsigned long)instance->generic.serial,
        (unsigned int)instance->generic.btn,
        code_alarm_button_name((uint8_t)instance->generic.btn));
}

static const SubGhzDecodeEntry code_alarm_items[] = {{.name = "Code Alarm", .get_string = code_alarm_decode}};
static const SubGhzDecodeRegistry code_alarm_registry = {.items = code_alarm_items, .size = 1};
static const FlipperAppPluginDescriptor decode_code_alarm_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &code_alarm_registry};
const FlipperAppPluginDescriptor* decode_code_alarm_ep(void) { return &decode_code_alarm_descriptor; }
