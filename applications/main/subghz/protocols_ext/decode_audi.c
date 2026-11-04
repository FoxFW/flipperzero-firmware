/* decode_audi.fal : SD get_string for audi (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/audi.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <furi.h>
#include <furi_hal.h>
#include <string.h>

#define AUDI_WIRE_BYTES   8U
#define AUDI_WIRE_BITS    64U
#define AUDI_BTN_0 0x01U
#define AUDI_BTN_1 0x02U
#define AUDI_BTN_2 0x04U

struct SubGhzProtocolDecoderAudi {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    // PWM demod scratch: we collect the HIGH duration then pair it with the
    // following LOW duration to classify one bit.
    uint32_t pending_high;
    bool have_high;

    uint8_t bytes[AUDI_WIRE_BYTES];
    uint16_t bit_count;

    uint8_t raw_data[AUDI_WIRE_BYTES]; // on-wire (scrambled) 8 bytes
    uint8_t last_raw_data[AUDI_WIRE_BYTES];
    bool last_raw_valid;
    bool parity_ok; // scramble inverse recovered a self-consistent block
};

static uint8_t audi_checksum(const uint8_t raw[AUDI_WIRE_BYTES]);
static const char* audi_button_name(uint8_t btn);

static uint8_t audi_checksum(const uint8_t raw[AUDI_WIRE_BYTES]) {
    uint32_t sum = 0U;
    for(size_t i = 0; i < 7U; i++) sum += raw[i];
    return (uint8_t)(sum & 0xFFU);
}

static const char* audi_button_name(uint8_t btn) {
    switch(btn) {
    case AUDI_BTN_0:
        return "Lock";
    case AUDI_BTN_1:
        return "Unlock";
    case AUDI_BTN_2:
        return "Trunk";
    default:
        return "Unknown";
    }
}

static void audi_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderAudi* instance = input->decoder;


    const uint8_t cs = instance->raw_data[7];
    const uint8_t cs_calc = audi_checksum(instance->raw_data);

    furi_string_cat_printf(
        output,
        "%s %ubit\r\n"
        "SN:%06lX Btn:[%s]\r\n"
        "Cnt:%04lX CS:%02X[%s] P:%s\r\n"
        "Data:%02X%02X%02X%02X%02X%02X%02X%02X\r\n",
        instance->generic.protocol_name,
        AUDI_WIRE_BITS,
        (unsigned long)instance->generic.serial,
        audi_button_name((uint8_t)instance->generic.btn),
        (unsigned long)instance->generic.cnt,
        cs,
        (cs == cs_calc) ? "OK" : "opaque",
        instance->parity_ok ? "OK" : "BAD",
        instance->raw_data[0],
        instance->raw_data[1],
        instance->raw_data[2],
        instance->raw_data[3],
        instance->raw_data[4],
        instance->raw_data[5],
        instance->raw_data[6],
        instance->raw_data[7]);
}

static const SubGhzDecodeEntry audi_items[] = {{.name = "Audi", .get_string = audi_decode}};
static const SubGhzDecodeRegistry audi_registry = {.items = audi_items, .size = 1};
static const FlipperAppPluginDescriptor decode_audi_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &audi_registry};
const FlipperAppPluginDescriptor* decode_audi_ep(void) { return &decode_audi_descriptor; }
