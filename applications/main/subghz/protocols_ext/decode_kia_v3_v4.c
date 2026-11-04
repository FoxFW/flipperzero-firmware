/* decode_kia_v3_v4.fal : SD get_string for kia_v3_v4 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/kia_v3_v4.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/keeloq_common.h>

struct SubGhzProtocolDecoderKiaV3V4 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;

    uint8_t raw_bits[32];
    uint16_t raw_bit_count;
    bool is_v3_sync;

    uint32_t encrypted;
    uint32_t decrypted;
    uint8_t crc;
    uint8_t version;
};

static const char* kia_version_names[] = {"Kia V4", "Kia V3"};

static const char* subghz_protocol_kia_v3_v4_get_name_button(uint8_t btn);
static uint64_t compute_yek(uint64_t key);
static uint8_t kia_v3_v4_ui_button(uint8_t custom, uint8_t original_btn);

static const char* subghz_protocol_kia_v3_v4_get_name_button(uint8_t btn) {
    switch(btn) {
    case 0x1:
        return "Lock";
    case 0x2:
        return "Unlock";
    case 0x3:
        return "Trunk";
    case 0x4:
        return "Panic";
    case 0x8:
        return "Horn";
    default:
        return "Unknown";
    }
}

static uint64_t compute_yek(uint64_t key) {
    uint64_t yek = 0;
    for(int i = 0; i < 64; i++) {
        yek |= ((key >> i) & 1) << (63 - i);
    }
    return yek;
}

static uint8_t kia_v3_v4_ui_button(uint8_t custom, uint8_t original_btn) {
    switch(custom) {
    case SUBGHZ_CUSTOM_BTN_UP:
        return 0x1U;
    case SUBGHZ_CUSTOM_BTN_DOWN:
        return 0x2U;
    case SUBGHZ_CUSTOM_BTN_LEFT:
        return 0x3U;
    case SUBGHZ_CUSTOM_BTN_RIGHT:
        return 0x4U;
    case SUBGHZ_CUSTOM_BTN_OK:
    default:
        return original_btn;
    }
}

static void kia_v3_v4_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderKiaV3V4* instance = input->decoder;


    uint64_t yek = compute_yek(instance->generic.data);
    uint32_t key_hi = (uint32_t)(instance->generic.data >> 32);
    uint32_t key_lo = (uint32_t)(instance->generic.data & 0xFFFFFFFF);
    uint32_t yek_hi = (uint32_t)(yek >> 32);
    uint32_t yek_lo = (uint32_t)(yek & 0xFFFFFFFF);

    subghz_custom_btn_set_max(4);
    uint8_t display_btn = (uint8_t)instance->generic.btn;
    uint8_t custom_btn_id = subghz_custom_btn_get();
    if(custom_btn_id != SUBGHZ_CUSTOM_BTN_OK) {
        display_btn = kia_v3_v4_ui_button(custom_btn_id, (uint8_t)instance->generic.btn);
    }

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Yek:%08lX%08lX\r\n"
        "Serial:%07lX Btn:%01X [%s]\r\n"
        "Cnt:%04lX CRC:%01X\r\n",
        kia_version_names[instance->version],
        instance->generic.data_count_bit,
        key_hi,
        key_lo,
        yek_hi,
        yek_lo,
        instance->generic.serial,
        display_btn,
        subghz_protocol_kia_v3_v4_get_name_button(display_btn),
        instance->generic.cnt,
        instance->crc);
}

static const SubGhzDecodeEntry kia_v3_v4_items[] = {{.name = "KIA/HYU V3/V4", .get_string = kia_v3_v4_decode}};
static const SubGhzDecodeRegistry kia_v3_v4_registry = {.items = kia_v3_v4_items, .size = 1};
static const FlipperAppPluginDescriptor decode_kia_v3_v4_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &kia_v3_v4_registry};
const FlipperAppPluginDescriptor* decode_kia_v3_v4_ep(void) { return &decode_kia_v3_v4_descriptor; }
