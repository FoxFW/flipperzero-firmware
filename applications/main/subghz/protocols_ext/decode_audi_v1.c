/* decode_audi_v1.fal : SD get_string for audi_v1 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/audi_v1.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <furi.h>
#include <furi_hal.h>

#define AUDI_V1_BTN_LOCK   0x0E
#define AUDI_V1_BTN_UNLOCK 0x0B

struct SubGhzProtocolDecoderAudiV1 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t preamble_count;
    ManchesterState manchester_saved_state;
    uint64_t hop;
    uint8_t btn_byte;
};

static uint8_t audi_v1_extract_counter(uint64_t hop, uint8_t btn_byte);
static const char* audi_v1_get_button_name(uint8_t btn_id);
static void audi_v1_extract_fields(SubGhzBlockGeneric* generic, uint64_t hop, uint8_t btn_byte);

static uint8_t audi_v1_extract_counter(uint64_t hop, uint8_t btn_byte) {
    uint8_t hop_bits = (hop >> 42) & 0x3F;
    uint8_t btn_lo = btn_byte & 0x03;
    return hop_bits | (btn_lo << 6);
}

static const char* audi_v1_get_button_name(uint8_t btn_id) {
    switch(btn_id) {
    case AUDI_V1_BTN_LOCK:
        return "Lock";
    case AUDI_V1_BTN_UNLOCK:
        return "Unlock";
    default:
        return "Unknown";
    }
}

static void audi_v1_extract_fields(SubGhzBlockGeneric* generic, uint64_t hop, uint8_t btn_byte) {
    generic->serial = (uint32_t)(generic->data >> 32);
    generic->btn = (btn_byte >> 4) & 0x0F;
    generic->cnt = audi_v1_extract_counter(hop, btn_byte);
}

static void audi_v1_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderAudiV1* instance = input->decoder;


    audi_v1_extract_fields(&instance->generic, instance->hop, instance->btn_byte);

    uint8_t btn_id = (instance->btn_byte >> 4) & 0x0F;
    const char* btn_name = audi_v1_get_button_name(btn_id);

    uint32_t key_hi = (uint32_t)(instance->generic.data >> 32);
    uint32_t key_lo = (uint32_t)(instance->generic.data & 0xFFFFFFFF);
    uint32_t hop_hi = (uint32_t)(instance->hop >> 24);
    uint32_t hop_lo = (uint32_t)(instance->hop & 0xFFFFFF);

    furi_string_cat_printf(
        output,
        "%s 88bit\r\n"
        "Key:%08lX%08lX\r\n"
        "Sn:%08lX Btn:%s\r\n"
        "Cnt:%03lu Hop:%06lX%06lX\r\n",
        instance->generic.protocol_name,
        (unsigned long)key_hi,
        (unsigned long)key_lo,
        (unsigned long)instance->generic.serial,
        btn_name,
        (unsigned long)instance->generic.cnt,
        (unsigned long)hop_hi,
        (unsigned long)hop_lo);
}

static const SubGhzDecodeEntry audi_v1_items[] = {{.name = "Audi V1", .get_string = audi_v1_decode}};
static const SubGhzDecodeRegistry audi_v1_registry = {.items = audi_v1_items, .size = 1};
static const FlipperAppPluginDescriptor decode_audi_v1_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &audi_v1_registry};
const FlipperAppPluginDescriptor* decode_audi_v1_ep(void) { return &decode_audi_v1_descriptor; }
