/* decode_mazda_siemens.fal : SD get_string for mazda_siemens (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/mazda_siemens.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define MAZDA_DATA_BUFFER_SIZE 14

struct SubGhzProtocolDecoderMazdaSiemens {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t preamble_count;
    uint16_t bit_counter;
    uint8_t prev_state;
    uint8_t data_buffer[MAZDA_DATA_BUFFER_SIZE];
};

static void mazda_parse_data(SubGhzBlockGeneric* instance);
static const char* mazda_get_btn_name(uint8_t btn);

static void mazda_parse_data(SubGhzBlockGeneric* instance) {
    instance->serial = (uint32_t)(instance->data >> 32);
    instance->btn = (instance->data >> 24) & 0xFF;
    instance->cnt = (instance->data >> 8) & 0xFFFF;
}

static const char* mazda_get_btn_name(uint8_t btn) {
    switch(btn) {
    case 0x10:
        return "Lock";
    case 0x20:
        return "Unlock";
    case 0x40:
        return "Trunk";
    default:
        return "Unknown";
    }
}

static void mazda_siemens_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderMazdaSiemens* instance = input->decoder;

    mazda_parse_data(&instance->generic);

    subghz_block_generic_global.btn_is_available = false;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 8;

    uint8_t data[8];
    for(int i = 0; i < 8; i++) {
        data[i] = (instance->generic.data >> (56 - 8 * i)) & 0xFF;
    }

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%02X %02X %02X %02X %02X %02X %02X %02X\r\n"
        "Sn:%08lX Btn:%s\r\n"
        "Cnt:%04lX Chk:%02X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        data[0],
        data[1],
        data[2],
        data[3],
        data[4],
        data[5],
        data[6],
        data[7],
        (uint32_t)instance->generic.serial,
        mazda_get_btn_name(instance->generic.btn),
        (uint32_t)instance->generic.cnt,
        data[7]);
}

static const SubGhzDecodeEntry mazda_siemens_items[] = {{.name = "MazdaSiemens", .get_string = mazda_siemens_decode}};
static const SubGhzDecodeRegistry mazda_siemens_registry = {.items = mazda_siemens_items, .size = 1};
static const FlipperAppPluginDescriptor decode_mazda_siemens_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &mazda_siemens_registry};
const FlipperAppPluginDescriptor* decode_mazda_siemens_ep(void) { return &decode_mazda_siemens_descriptor; }
