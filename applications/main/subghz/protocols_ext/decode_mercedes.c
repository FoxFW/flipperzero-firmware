/* decode_mercedes.fal : SD get_string for mercedes (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/mercedes.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <furi.h>
#include <string.h>

#define MERCEDES_WIRE_BYTES    14U
#define MERCEDES_WIRE_BITS     112U

struct SubGhzProtocolDecoderMercedes {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t preamble_count;

    ManchesterState manchester_state;
    uint8_t bytes[MERCEDES_WIRE_BYTES];
    uint16_t bit_count;

    uint8_t raw_data[MERCEDES_WIRE_BYTES];
    uint8_t last_raw_data[MERCEDES_WIRE_BYTES];
    bool last_raw_valid;
};

static uint8_t mercedes_checksum(const uint8_t raw[MERCEDES_WIRE_BYTES]);

static uint8_t mercedes_checksum(const uint8_t raw[MERCEDES_WIRE_BYTES]) {
    uint32_t sum = 0U;
    for(size_t i = 0U; i < 7U; i++) {
        sum += raw[i];
    }
    return (uint8_t)(sum & 0xFFU);
}

static void mercedes_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderMercedes* instance = input->decoder;


    const uint8_t cs = instance->raw_data[7];
    const uint8_t cs_calc = mercedes_checksum(instance->raw_data);

    furi_string_cat_printf(
        output,
        "%s %ubit\r\n"
        "SN:%08lX Btn:%02X\r\n"
        "Cnt:%04lX CS:%02X[%s]\r\n"
        "Data:%02X%02X%02X%02X%02X%02X%02X\r\n"
        "     %02X%02X%02X%02X%02X%02X%02X\r\n",
        instance->generic.protocol_name,
        MERCEDES_WIRE_BITS,
        (unsigned long)instance->generic.serial,
        instance->generic.btn,
        (unsigned long)instance->generic.cnt,
        cs,
        (cs == cs_calc) ? "OK" : "opaque",
        instance->raw_data[0],
        instance->raw_data[1],
        instance->raw_data[2],
        instance->raw_data[3],
        instance->raw_data[4],
        instance->raw_data[5],
        instance->raw_data[6],
        instance->raw_data[7],
        instance->raw_data[8],
        instance->raw_data[9],
        instance->raw_data[10],
        instance->raw_data[11],
        instance->raw_data[12],
        instance->raw_data[13]);
}

static const SubGhzDecodeEntry mercedes_items[] = {{.name = "Mercedes", .get_string = mercedes_decode}};
static const SubGhzDecodeRegistry mercedes_registry = {.items = mercedes_items, .size = 1};
static const FlipperAppPluginDescriptor decode_mercedes_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &mercedes_registry};
const FlipperAppPluginDescriptor* decode_mercedes_ep(void) { return &decode_mercedes_descriptor; }
