/* decode_honda_v7.fal : SD get_string for honda_v7 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/honda_v7.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderHondaV7 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t parser_step;
    uint16_t pre;
    uint16_t bit_count;
    uint32_t last_high;
    uint8_t buf[16];

    uint32_t serial;
    uint8_t btn;
    uint32_t cnt;
    uint8_t mode;
    uint8_t crc;
    bool crc_ok;
    uint32_t tail; // natural[8..10] (24 bits incl checksum)
};

static const char* honda_v7_btn_name(uint8_t btn);

static const char* honda_v7_btn_name(uint8_t btn) {
    switch(btn) {
    case 0x0: return "Panic";
    case 0x1: return "Lock";
    case 0x2: return "Trunk";
    case 0x4: return "Remote Start";
    case 0x8: return "Unlock";
    case 0x9: return "Unlock x2";
    default: return "?";
    }
}

static void honda_v7_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderHondaV7* instance = input->decoder;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%016llX\r\n"
        "Serial:%07lX Btn:%X %s\r\n"
        "Cnt:%06lX Mode:%X\r\n"
        "CRC:%02X %s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long long)instance->generic.data,
        (unsigned long)instance->serial,
        instance->btn,
        honda_v7_btn_name(instance->btn),
        (unsigned long)instance->cnt,
        instance->mode,
        instance->crc,
        instance->crc_ok ? "OK" : "--");
}

static const SubGhzDecodeEntry honda_v7_items[] = {{.name = "Honda V7", .get_string = honda_v7_decode}};
static const SubGhzDecodeRegistry honda_v7_registry = {.items = honda_v7_items, .size = 1};
static const FlipperAppPluginDescriptor decode_honda_v7_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &honda_v7_registry};
const FlipperAppPluginDescriptor* decode_honda_v7_ep(void) { return &decode_honda_v7_descriptor; }
