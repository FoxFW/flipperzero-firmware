/* decode_gm.fal : SD get_string for gm (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/gm.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <furi.h>
#include <string.h>

#define GM_WIRE_BYTES     14U
#define GM_WIRE_BITS      112U

struct SubGhzProtocolDecoderGM {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t bits[GM_WIRE_BYTES];
    uint16_t bit_count;

    uint8_t raw_data[GM_WIRE_BYTES];
    uint8_t last_raw_data[GM_WIRE_BYTES];
    bool last_raw_valid;
};

static uint32_t gm_add_bytes(const uint8_t* b, size_t n);
static uint32_t gm_sequence(const uint8_t raw[GM_WIRE_BYTES]);
static uint8_t gm_button(const uint8_t raw[GM_WIRE_BYTES]);
static const char* gm_button_name(uint8_t button);

static uint32_t gm_add_bytes(const uint8_t* b, size_t n) {
    uint32_t sum = 0U;
    for(size_t i = 0U; i < n; i++) {
        sum += b[i];
    }
    return sum;
}

static uint32_t gm_sequence(const uint8_t raw[GM_WIRE_BYTES]) {
    return ((uint32_t)raw[7] << 16U) | ((uint32_t)raw[8] << 8U) | raw[9];
}

static uint8_t gm_button(const uint8_t raw[GM_WIRE_BYTES]) {
    return (uint8_t)(raw[2] & 0x07U);
}

static const char* gm_button_name(uint8_t button) {
    switch(button) {
    case 0x1U: return "Unlock";
    case 0x2U: return "Lock";
    case 0x3U: return "Trunk";
    case 0x4U: return "Panic";
    default:   return "?";
    }
}

static void gm_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderGM* instance = input->decoder;


    const uint8_t full_ck = instance->raw_data[13];
    const bool full_ck_ok = ((gm_add_bytes(&instance->raw_data[1], 13U) & 0xFFU) == 0U);

    furi_string_cat_printf(
        output,
        "%s %ubit\r\n"
        "SN:%08lX Btn:%X[%s]\r\n"
        "Seq:%06lX CS:%02X[%s]\r\n"
        "Data:%02X%02X%02X%02X%02X%02X%02X\r\n"
        "     %02X%02X%02X%02X%02X%02X%02X\r\n",
        instance->generic.protocol_name,
        GM_WIRE_BITS,
        (unsigned long)instance->generic.serial,
        (unsigned)gm_button(instance->raw_data),
        gm_button_name(gm_button(instance->raw_data)),
        (unsigned long)gm_sequence(instance->raw_data),
        full_ck,
        full_ck_ok ? "OK" : "ERR",
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

static const SubGhzDecodeEntry gm_items[] = {{.name = "GM", .get_string = gm_decode}};
static const SubGhzDecodeRegistry gm_registry = {.items = gm_items, .size = 1};
static const FlipperAppPluginDescriptor decode_gm_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &gm_registry};
const FlipperAppPluginDescriptor* decode_gm_ep(void) { return &decode_gm_descriptor; }
