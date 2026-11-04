/* decode_kia_v7.fal : SD get_string for kia_v7 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/kia_v7.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <string.h>

#define KIA_V7_KEY_BITS           64U

struct SubGhzProtocolDecoderKiaV7 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;
    uint16_t preamble_count;

    uint8_t decoded_button;
    uint8_t fixed_high_byte;
    uint8_t crc_calculated;
    uint8_t crc_raw;
    bool crc_valid;
};

static uint8_t kia_v7_crc8(const uint8_t* data, size_t len);
static void kia_v7_u64_to_bytes_be(uint64_t data, uint8_t bytes[8]);
static const char* kia_v7_get_button_name(uint8_t button);
static void kia_v7_decode_key_common(
    SubGhzBlockGeneric* generic,
    uint8_t* decoded_button,
    uint8_t* fixed_high_byte,
    uint8_t* crc_calculated,
    uint8_t* crc_raw,
    bool* crc_valid);
static void kia_v7_decode_key_decoder(SubGhzProtocolDecoderKiaV7* instance);

static uint8_t kia_v7_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0x4CU;

    for(size_t index = 0; index < len; index++) {
        crc ^= data[index];
        for(uint8_t bit = 0; bit < 8; bit++) {
            const bool msb = (crc & 0x80U) != 0U;
            crc <<= 1U;
            if(msb) {
                crc ^= 0x7FU;
            }
        }
    }

    return crc;
}

static void kia_v7_u64_to_bytes_be(uint64_t data, uint8_t bytes[8]) {
    for(size_t index = 0; index < 8; index++) {
        bytes[index] = (data >> ((7U - index) * 8U)) & 0xFFU;
    }
}

static const char* kia_v7_get_button_name(uint8_t button) {
    switch(button) {
    case 0x01:
        return "LOCK";
    case 0x02:
        return "UNLOCK";
    case 0x03:
    case 0x08:
        return "BOOT";
    default:
        return "??";
    }
}

static void kia_v7_decode_key_common(
    SubGhzBlockGeneric* generic,
    uint8_t* decoded_button,
    uint8_t* fixed_high_byte,
    uint8_t* crc_calculated,
    uint8_t* crc_raw,
    bool* crc_valid) {
    uint8_t bytes[8];
    kia_v7_u64_to_bytes_be(generic->data, bytes);

    const uint32_t serial = (((uint32_t)bytes[3]) << 20U) | (((uint32_t)bytes[4]) << 12U) |
                            (((uint32_t)bytes[5]) << 4U) | (((uint32_t)bytes[6]) >> 4U);
    const uint16_t counter = ((uint16_t)bytes[1] << 8U) | (uint16_t)bytes[2];
    const uint8_t button = bytes[6] & 0x0FU;
    const uint8_t crc_calc = kia_v7_crc8(bytes, 7);
    const uint8_t crc_pkt = bytes[7];

    generic->serial = serial & 0x0FFFFFFFU;
    generic->btn = button;
    generic->cnt = counter;
    generic->data_count_bit = KIA_V7_KEY_BITS;

    if(decoded_button) {
        *decoded_button = button;
    }
    if(fixed_high_byte) {
        *fixed_high_byte = bytes[0];
    }
    if(crc_calculated) {
        *crc_calculated = crc_calc;
    }
    if(crc_raw) {
        *crc_raw = crc_pkt;
    }
    if(crc_valid) {
        *crc_valid = (crc_calc == crc_pkt);
    }
}

static void kia_v7_decode_key_decoder(SubGhzProtocolDecoderKiaV7* instance) {
    kia_v7_decode_key_common(
        &instance->generic,
        &instance->decoded_button,
        &instance->fixed_high_byte,
        &instance->crc_calculated,
        &instance->crc_raw,
        &instance->crc_valid);
}

static void kia_v7_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderKiaV7* instance = input->decoder;

    kia_v7_decode_key_decoder(instance);

    subghz_custom_btn_set_max(4);
    uint8_t display_btn = (uint8_t)(instance->decoded_button & 0x0FU);
    switch(subghz_custom_btn_get()) {
    case SUBGHZ_CUSTOM_BTN_UP:
        display_btn = 0x01U;
        break;
    case SUBGHZ_CUSTOM_BTN_DOWN:
        display_btn = 0x02U;
        break;
    case SUBGHZ_CUSTOM_BTN_LEFT:
        display_btn = 0x03U;
        break;
    case SUBGHZ_CUSTOM_BTN_OK:
    case SUBGHZ_CUSTOM_BTN_RIGHT:
    default:
        break;
    }

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%016llX\r\n"
        "Sn:%07lX Cnt:%04lX\r\n"
        "Btn:%01X [%s] CRC:%02X [%s]",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        instance->generic.data,
        instance->generic.serial & 0x0FFFFFFFU,
        instance->generic.cnt & 0xFFFFU,
        display_btn,
        kia_v7_get_button_name(display_btn),
        instance->crc_calculated,
        instance->crc_valid ? "OK" : "ERR");
}

static const SubGhzDecodeEntry kia_v7_items[] = {{.name = "Kia V7", .get_string = kia_v7_decode}};
static const SubGhzDecodeRegistry kia_v7_registry = {.items = kia_v7_items, .size = 1};
static const FlipperAppPluginDescriptor decode_kia_v7_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &kia_v7_registry};
const FlipperAppPluginDescriptor* decode_kia_v7_ep(void) { return &decode_kia_v7_descriptor; }
