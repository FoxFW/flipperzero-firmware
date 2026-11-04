/* decode_ford_v3.fal : SD get_string for ford_v3 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/blocks/bitbuf.h>
#include <lib/subghz/protocols/ford_v3.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <string.h>
#include <stdlib.h>

#define FORD_V3_DATA_BYTES     13U
#define FORD_V3_CELL_CAP       320U
#define FORD_V3_BTN_LOCK   0x01U
#define FORD_V3_BTN_UNLOCK 0x02U
#define FORD_V3_VARIANT_EU 0U
#define FORD_V3_VARIANT_US 1U

typedef struct SubGhzProtocolDecoderFordV3 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;
    uint8_t manchester_raw[FORD_V3_DATA_BYTES];
    uint8_t manchester_bit_count;
    uint16_t preamble_count;

    uint8_t cells[SUBGHZ_BITBUF_BYTES(FORD_V3_CELL_CAP)];
    uint16_t cell_count;

    uint8_t raw_bytes[FORD_V3_DATA_BYTES];
    uint8_t last_raw_bytes[FORD_V3_DATA_BYTES];
    bool last_raw_valid;

    uint8_t variant;
    uint8_t flag;
    uint32_t serial;
    uint16_t counter;
} SubGhzProtocolDecoderFordV3;

static const char* ford_v3_button_name(uint8_t btn, uint8_t variant);

static const char* ford_v3_button_name(uint8_t btn, uint8_t variant) {
    if(variant == FORD_V3_VARIANT_US) {
        switch(btn) {
        case FORD_V3_BTN_LOCK:
            return "Lock";
        case FORD_V3_BTN_UNLOCK:
            return "Unlock";
        default:
            return "?";
        }
    }

    switch(btn) {
    case FORD_V3_BTN_LOCK:
        return "Lock";
    case FORD_V3_BTN_UNLOCK:
        return "Unlock";
    default:
        return "?";
    }
}

static void ford_v3_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderFordV3* instance = input->decoder;

    const uint8_t* k = instance->raw_bytes;

    if(instance->variant == FORD_V3_VARIANT_US) {
        furi_string_cat_printf(
            output,
            "%s US %dbit\r\n"
            "Key:%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\r\n"
            "Sn:%08lX Btn:%02X %s\r\n"
            "Cnt:%04X Hop:%02X%02X%02X%02X\r\n",
            instance->generic.protocol_name,
            (int)instance->generic.data_count_bit,
            k[0],
            k[1],
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
            ford_v3_button_name(instance->generic.btn, FORD_V3_VARIANT_US),
            (unsigned)instance->counter,
            k[9],
            k[10],
            k[11],
            k[12]);
        return;
    }

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\r\n"
        "Sn:%08lX Btn:%02X %s\r\n"
        "Cnt:%04X Hop:%02X%02X%02X%02X\r\n",
        instance->generic.protocol_name,
        (int)instance->generic.data_count_bit,
        k[0],
        k[1],
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
        ford_v3_button_name(instance->generic.btn, FORD_V3_VARIANT_EU),
        (unsigned)instance->counter,
        k[9],
        k[10],
        k[11],
        k[12]);
}

static const SubGhzDecodeEntry ford_v3_items[] = {{.name = "Ford V3", .get_string = ford_v3_decode}};
static const SubGhzDecodeRegistry ford_v3_registry = {.items = ford_v3_items, .size = 1};
static const FlipperAppPluginDescriptor decode_ford_v3_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &ford_v3_registry};
const FlipperAppPluginDescriptor* decode_ford_v3_ep(void) { return &decode_ford_v3_descriptor; }
