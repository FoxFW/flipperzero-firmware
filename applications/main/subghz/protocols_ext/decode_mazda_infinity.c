/* decode_mazda_infinity.fal : SD get_string for mazda_infinity (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/mazda_infinity.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <furi.h>
#include <furi_hal.h>
#include <string.h>

#define MAZDA_INF_PAYLOAD_BYTES  8U
#define MAZDA_INF_PAYLOAD_BITS   64U
#define MAZDA_INF_BTN_LOCK   0x10U
#define MAZDA_INF_BTN_UNLOCK 0x20U
#define MAZDA_INF_BTN_TRUNK  0x40U
#define MAZDA_INF_WINDOW_BYTES 12U
#define MAZDA_INF_WINDOW_CELLS (MAZDA_INF_WINDOW_BYTES * 8U * 2U)

struct SubGhzProtocolDecoderMazdaInfinity {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t cells[MAZDA_INF_WINDOW_CELLS];
    uint16_t cell_count;

    uint8_t logical[MAZDA_INF_PAYLOAD_BYTES]; // de-whitened logical payload
    uint8_t last_logical[MAZDA_INF_PAYLOAD_BYTES];
    bool last_valid;
    bool checksum_ok;
};

static const char* mazda_inf_button_name(uint8_t btn);

static const char* mazda_inf_button_name(uint8_t btn) {
    switch(btn) {
    case MAZDA_INF_BTN_LOCK:
        return "Lock";
    case MAZDA_INF_BTN_UNLOCK:
        return "Unlock";
    case MAZDA_INF_BTN_TRUNK:
        return "Trunk";
    default:
        return "Unknown";
    }
}

static void mazda_infinity_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderMazdaInfinity* instance = input->decoder;


    furi_string_cat_printf(
        output,
        "%s %ubit\r\n"
        "SN:%08lX Btn:[%s]\r\n"
        "Cnt:%04lX CS:%02X[%s]\r\n"
        "Data:%02X%02X%02X%02X%02X%02X%02X%02X\r\n",
        instance->generic.protocol_name,
        MAZDA_INF_PAYLOAD_BITS,
        (unsigned long)instance->generic.serial,
        mazda_inf_button_name((uint8_t)instance->generic.btn),
        (unsigned long)instance->generic.cnt,
        instance->logical[7],
        instance->checksum_ok ? "OK" : "BAD",
        instance->logical[0],
        instance->logical[1],
        instance->logical[2],
        instance->logical[3],
        instance->logical[4],
        instance->logical[5],
        instance->logical[6],
        instance->logical[7]);
}

static const SubGhzDecodeEntry mazda_infinity_items[] = {{.name = "Mazda Infinity", .get_string = mazda_infinity_decode}};
static const SubGhzDecodeRegistry mazda_infinity_registry = {.items = mazda_infinity_items, .size = 1};
static const FlipperAppPluginDescriptor decode_mazda_infinity_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &mazda_infinity_registry};
const FlipperAppPluginDescriptor* decode_mazda_infinity_ep(void) { return &decode_mazda_infinity_descriptor; }
