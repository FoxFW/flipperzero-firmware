/* decode_mazda_v0.fal : SD get_string for mazda_v0 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/mazda_v0.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <string.h>

typedef struct SubGhzProtocolDecoderMazdaV0 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;
    uint16_t preamble_count;
    uint8_t preamble_pattern;

    uint32_t serial;
    uint8_t button;
    uint32_t count;
} 
SubGhzProtocolDecoderMazdaV0;

static const SubGhzBlockConst subghz_protocol_mazda_v0_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 100,
    .min_count_bit_for_found = 64,
};

static uint8_t mazda_v0_popcount8(uint8_t x);
static void mazda_v0_u64_to_bytes_be(uint64_t data, uint8_t bytes[8]);
static uint8_t mazda_v0_calculate_checksum(uint32_t serial, uint8_t button, uint32_t counter);
static const char* mazda_v0_get_button_name(uint8_t button);
static void mazda_v0_decode_key(SubGhzBlockGeneric* generic);

static uint8_t mazda_v0_popcount8(uint8_t x) {
    uint8_t count = 0;
    while(x) {
        count += x & 1;
        x >>= 1;
    }
    return count;
}

static void mazda_v0_u64_to_bytes_be(uint64_t data, uint8_t bytes[8]) {
    for(size_t i = 0; i < 8; i++) {
        bytes[i] = (uint8_t)((data >> ((7 - i) * 8)) & 0xFF);
    }
}

static uint8_t mazda_v0_calculate_checksum(uint32_t serial, uint8_t button, uint32_t counter) {
    counter &= 0xFFFFFU;
    return (uint8_t)(((serial >> 24) & 0xFF) + ((serial >> 16) & 0xFF) + ((serial >> 8) & 0xFF) +
                     (serial & 0xFF) + ((counter >> 8) & 0xFF) + (counter & 0xFF) +
                     ((((counter >> 16) & 0x0F) | ((button & 0x0F) << 4)) & 0xFF));
}

static const char* mazda_v0_get_button_name(uint8_t button) {
    switch(button) {
    case 0x01:
        return "LOCK";
    case 0x02:
        return "UNLOCK";
    case 0x04:
        return "BOOT";
    case 0x08:
        return "REMOTE";
    default:
        return "??";
    }
}

static void mazda_v0_decode_key(SubGhzBlockGeneric* generic) {
    uint8_t data[8];
    mazda_v0_u64_to_bytes_be(generic->data, data);

    const bool parity = (mazda_v0_popcount8(data[7]) & 1) != 0;
    const uint8_t limit = parity ? 6 : 5;
    const uint8_t mask = data[limit];

    for(uint8_t i = 0; i < limit; i++) {
        data[i] ^= mask;
    }

    if(!parity) {
        data[6] ^= mask;
    }

    const uint8_t counter_lo = (data[5] & 0x55) | (data[6] & 0xAA);
    const uint8_t counter_mid = (data[6] & 0x55) | (data[5] & 0xAA);

    generic->serial = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                      ((uint32_t)data[2] << 8) | (uint32_t)data[3];
    generic->btn = (data[4] >> 4) & 0x0F;
    generic->cnt = (((uint32_t)data[4] & 0x0F) << 16) | ((uint32_t)counter_mid << 8) |
                   (uint32_t)counter_lo;
    generic->data_count_bit = subghz_protocol_mazda_v0_const.min_count_bit_for_found;
}

static void mazda_v0_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderMazdaV0* instance = input->decoder;


    mazda_v0_decode_key(&instance->generic);

    subghz_custom_btn_set_max(4);
    const uint8_t original_btn = (uint8_t)(instance->generic.btn & 0x0FU);
    uint8_t display_btn = original_btn;
    switch(subghz_custom_btn_get()) {
    case SUBGHZ_CUSTOM_BTN_UP:    display_btn = 0x1U; break;
    case SUBGHZ_CUSTOM_BTN_OK:    display_btn = 0x2U; break;
    case SUBGHZ_CUSTOM_BTN_DOWN:  display_btn = 0x4U; break;
    case SUBGHZ_CUSTOM_BTN_RIGHT: display_btn = 0x8U; break;
    default:                      display_btn = original_btn; break;
    }

    const uint8_t calc_crc = mazda_v0_calculate_checksum(
        instance->generic.serial, display_btn, instance->generic.cnt);
    const uint8_t raw_crc = (display_btn == original_btn) ?
                                (uint8_t)(instance->generic.data & 0xFF) :
                                calc_crc;

    furi_string_cat_printf(
        output,
        "%s %dbit CRC:%s\r\n"
        "Key: %016llX\r\n"
        "Sn: %08lX  Btn: %02X - %s\r\n"
        "Cnt: %05lX  Chk: %02X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (raw_crc == calc_crc) ? "OK" : "BAD",
        (unsigned long long)instance->generic.data,
        (unsigned long)instance->generic.serial,
        display_btn,
        mazda_v0_get_button_name(display_btn),
        (unsigned long)(instance->generic.cnt & 0xFFFFFU),
        raw_crc);
}

static const SubGhzDecodeEntry mazda_v0_items[] = {{.name = "Mazda V0", .get_string = mazda_v0_decode}};
static const SubGhzDecodeRegistry mazda_v0_registry = {.items = mazda_v0_items, .size = 1};
static const FlipperAppPluginDescriptor decode_mazda_v0_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &mazda_v0_registry};
const FlipperAppPluginDescriptor* decode_mazda_v0_ep(void) { return &decode_mazda_v0_descriptor; }
