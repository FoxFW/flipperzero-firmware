/* decode_hundai.fal : SD get_string for hundai (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/blocks/bitbuf.h>
#include <lib/subghz/protocols/hundai.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <furi.h>
#include <furi_hal.h>
#include <string.h>

#define HUNDAI_PREAMBLE_BYTES 32U
#define HUNDAI_WIRE_BYTES     9U
#define HUNDAI_WIRE_BITS      72U
#define HUNDAI_BTN_0 0x01U
#define HUNDAI_BTN_1 0x02U
#define HUNDAI_BTN_2 0x09U
#define HUNDAI_CELL_CAPACITY \
    (((HUNDAI_PREAMBLE_BYTES + HUNDAI_WIRE_BYTES + 4U) * 8U * 2U) + 8U)

struct SubGhzProtocolDecoderHundai {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    // Manchester half-cell buffer. Each entry is a captured half-cell level
    // (0/1); merged long pulses are split into two identical half-cells before
    // being pushed. Decoded from the tail at the end-of-frame gap.
    uint8_t cells[SUBGHZ_BITBUF_BYTES(HUNDAI_CELL_CAPACITY)];
    uint16_t cell_count;

    uint8_t raw_data[HUNDAI_WIRE_BYTES];

    // Duplicate/integrity check: the fob sends two identical frames per press.
    uint8_t prev_frame[HUNDAI_WIRE_BYTES];
    bool prev_frame_valid;
    bool dup_confirmed; // second identical frame was seen

    // Last reported frame, to suppress endless re-report of the same press.
    uint8_t last_reported[HUNDAI_WIRE_BYTES];
    bool last_reported_valid;
};

static uint8_t hundai_button(const uint8_t raw[HUNDAI_WIRE_BYTES]);
static uint8_t hundai_key_index(const uint8_t raw[HUNDAI_WIRE_BYTES]);
static uint8_t hundai_channel(const uint8_t raw[HUNDAI_WIRE_BYTES]);
static const char* hundai_button_name(uint8_t btn_nibble);

static uint8_t hundai_button(const uint8_t raw[HUNDAI_WIRE_BYTES]) {
    return (uint8_t)(raw[5] & 0x0FU);
}

static uint8_t hundai_key_index(const uint8_t raw[HUNDAI_WIRE_BYTES]) {
    return (uint8_t)((raw[7] >> 4U) & 0x0FU);
}

static uint8_t hundai_channel(const uint8_t raw[HUNDAI_WIRE_BYTES]) {
    return (uint8_t)(raw[7] & 0x0FU);
}

static const char* hundai_button_name(uint8_t btn_nibble) {
    switch(btn_nibble) {
    case HUNDAI_BTN_0:
        return "Lock";
    case HUNDAI_BTN_1:
        return "Unlock";
    case HUNDAI_BTN_2:
        return "Trunk";
    default:
        return "Unknown";
    }
}

static void hundai_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderHundai* instance = input->decoder;


    furi_string_cat_printf(
        output,
        "%s %ubit\r\n"
        "SN:%08lX Btn:[%s]\r\n"
        "Cnt:%02X KeyIdx:%X Ch:%X Dup:%s\r\n"
        "Data:%02X%02X%02X%02X%02X%02X%02X%02X%02X\r\n",
        instance->generic.protocol_name,
        HUNDAI_WIRE_BITS,
        (unsigned long)instance->generic.serial,
        hundai_button_name(hundai_button(instance->raw_data)),
        instance->raw_data[6],
        hundai_key_index(instance->raw_data),
        hundai_channel(instance->raw_data),
        instance->dup_confirmed ? "OK" : "single",
        instance->raw_data[0],
        instance->raw_data[1],
        instance->raw_data[2],
        instance->raw_data[3],
        instance->raw_data[4],
        instance->raw_data[5],
        instance->raw_data[6],
        instance->raw_data[7],
        instance->raw_data[8]);
}

static const SubGhzDecodeEntry hundai_items[] = {{.name = "Hundai", .get_string = hundai_decode}};
static const SubGhzDecodeRegistry hundai_registry = {.items = hundai_items, .size = 1};
static const FlipperAppPluginDescriptor decode_hundai_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &hundai_registry};
const FlipperAppPluginDescriptor* decode_hundai_ep(void) { return &decode_hundai_descriptor; }
