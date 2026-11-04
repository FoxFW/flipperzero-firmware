/* decode_ford_v2.fal : SD get_string for ford_v2 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/ford_v2.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <furi.h>
#include <string.h>

#define FORD_V2_DATA_BYTES             13U

typedef struct SubGhzProtocolDecoderFordV2 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;
    uint16_t preamble_count;

    uint8_t raw_bytes[FORD_V2_DATA_BYTES];
    uint8_t byte_count;

    uint16_t sync_shift;
    uint8_t sync_bit_count;

    uint64_t extra_data;
    uint16_t counter16;
    uint32_t tail31;
    bool structure_ok;
} SubGhzProtocolDecoderFordV2;

static const char* ford_v2_button_name(uint8_t btn);

static const char* ford_v2_button_name(uint8_t btn) {
    switch(btn) {
    case 0x10:
        return "Lock";
    case 0x11:
        return "Unlock";
    case 0x13:
        return "Trunk";
    case 0x14:
        return "Panic";
    case 0x15:
        return "RemoteStart";
    default:
        return "Unknown";
    }
}

static void ford_v2_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderFordV2* instance = input->decoder;

    const uint8_t* k = instance->raw_bytes;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\r\n"
        "Sn:%08lX Btn:%02X [%s]\r\n"
        "Cnt:%u Struct:%s\r\n"
        "Tail31:%08lX\r\n"
        "TailRaw:%02X%02X%02X%02X%02X\r\n",
        instance->generic.protocol_name,
        (int)instance->generic.data_count_bit,
        k[2],
        k[3],
        k[4],
        k[5],
        k[6],
        k[7],
        k[8],
        k[9],
        k[10],
        k[11],
        k[12],
        (unsigned long)instance->generic.serial,
        instance->generic.btn,
        ford_v2_button_name(instance->generic.btn),
        (unsigned)instance->counter16,
        instance->structure_ok ? "OK" : "BAD",
        (unsigned long)instance->tail31,
        k[8],
        k[9],
        k[10],
        k[11],
        k[12]);
}

static const SubGhzDecodeEntry ford_v2_items[] = {{.name = "Ford V2", .get_string = ford_v2_decode}};
static const SubGhzDecodeRegistry ford_v2_registry = {.items = ford_v2_items, .size = 1};
static const FlipperAppPluginDescriptor decode_ford_v2_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &ford_v2_registry};
const FlipperAppPluginDescriptor* decode_ford_v2_ep(void) { return &decode_ford_v2_descriptor; }
