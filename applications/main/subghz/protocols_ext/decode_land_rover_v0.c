/* decode_land_rover_v0.fal : SD get_string for land_rover_v0 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/land_rover_v0.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <string.h>

#define LAND_ROVER_V0_BTN_LOCK    0x02U
#define LAND_ROVER_V0_BTN_UNLOCK  0x04U

typedef struct SubGhzProtocolDecoderLandRoverV0 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder        decoder;
    SubGhzBlockGeneric        generic;

    uint16_t preamble_count;
    uint8_t  raw[10];
    uint8_t  bit_count;
    bool     extra_bit;
    bool     previous_bit;
    bool     boundary_pad_skipped;
    bool     pending_short;

    uint64_t key;
    uint16_t tail;
    uint32_t command_signature;
    uint32_t serial;
    uint32_t count;
    uint8_t  button;
    uint8_t  check;
    bool     check_ok;
    bool     tail_ok;
} SubGhzProtocolDecoderLandRoverV0;

static const char* land_rover_v0_button_name(uint8_t button);

static const char* land_rover_v0_button_name(uint8_t button) {
    switch(button) {
    case LAND_ROVER_V0_BTN_LOCK:   return "Lock";
    case LAND_ROVER_V0_BTN_UNLOCK: return "Unlock";
    default:                        return "Unknown";
    }
}

static void land_rover_v0_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderLandRoverV0* instance = input->decoder;


    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%016llX\r\n"
        "Sn:%06lX  Btn:%02X - %s\r\n"
        "BtnSig:%06lX\r\n"
        "Cnt:%05lX  Chk:%02X [%s]  Tail:%05lX [%s]\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long long)instance->key,
        (unsigned long)instance->serial,
        instance->button,
        land_rover_v0_button_name(instance->button),
        (unsigned long)instance->command_signature,
        (unsigned long)instance->count,
        instance->check,
        instance->check_ok ? "OK" : "BAD",
        (unsigned long)(((instance->tail >> 15) & 1U) ? 0x1FFFFUL : 0x0FFFFUL),
        instance->tail_ok ? "OK" : "BAD");
}

static const SubGhzDecodeEntry land_rover_v0_items[] = {{.name = "Land Rover V0", .get_string = land_rover_v0_decode}};
static const SubGhzDecodeRegistry land_rover_v0_registry = {.items = land_rover_v0_items, .size = 1};
static const FlipperAppPluginDescriptor decode_land_rover_v0_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &land_rover_v0_registry};
const FlipperAppPluginDescriptor* decode_land_rover_v0_ep(void) { return &decode_land_rover_v0_descriptor; }
