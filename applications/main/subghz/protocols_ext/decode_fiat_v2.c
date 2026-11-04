/* decode_fiat_v2.fal : SD get_string for fiat_v2 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/blocks/bitbuf.h>
#include <lib/subghz/protocols/fiat_v2.h>
#include <lib/subghz/protocols/fiat_v1.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <string.h>

#define FIAT_V2_WIRE_BITS         112U
#define FIAT_V2_WIRE_BYTES        14U
#define FIAT_V2_WIRE_CELLS        (FIAT_V2_WIRE_BITS * 2U)
#define FIAT_V2_LOGICAL_BITS      112U
#define FIAT_V2_BTN_SHIFT         6U
#define FIAT_V2_BUTTON_LOCK       0x2U
#define FIAT_V2_BUTTON_UNLOCK     0x3U
#define FIAT_V2_BUTTON_TRUNK      0x1U

struct SubGhzProtocolDecoderFiatV2 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t cells[SUBGHZ_BITBUF_BYTES(FIAT_V2_WIRE_CELLS)];
    uint16_t cell_count;

    uint8_t raw_data[FIAT_V2_WIRE_BYTES];
    uint8_t last_raw_data[FIAT_V2_WIRE_BYTES];
    bool last_raw_valid;

    uint32_t uid;
    uint32_t hop;
    uint8_t button;

    uint8_t hitag2_key[6];
    uint32_t hitag2_epoch;
    bool hitag2_key_valid;
    uint8_t hitag2_iv_combo;
};

static const char* fiat_v2_button_name(uint8_t button);
static uint8_t fiat_v2_ui_selector(uint8_t custom_btn_id, uint8_t original_selector);

static const char* fiat_v2_button_name(uint8_t button) {
    switch(button >> FIAT_V2_BTN_SHIFT) {
    case FIAT_V2_BUTTON_LOCK:
        return "Lock";
    case FIAT_V2_BUTTON_UNLOCK:
        return "Unlock";
    case FIAT_V2_BUTTON_TRUNK:
        return "Trunk";
    default:
        return "Unknown";
    }
}

static uint8_t fiat_v2_ui_selector(uint8_t custom_btn_id, uint8_t original_selector) {
    switch(custom_btn_id) {
    case SUBGHZ_CUSTOM_BTN_UP:
        return FIAT_V2_BUTTON_UNLOCK;
    case SUBGHZ_CUSTOM_BTN_DOWN:
        return FIAT_V2_BUTTON_LOCK;
    case SUBGHZ_CUSTOM_BTN_LEFT:
        return FIAT_V2_BUTTON_TRUNK;
    case SUBGHZ_CUSTOM_BTN_RIGHT:
    case SUBGHZ_CUSTOM_BTN_OK:
    default:
        return original_selector;
    }
}

static void fiat_v2_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderFiatV2* instance = input->decoder;


    if(instance->hitag2_key_valid) {
        subghz_custom_btn_set_max(3);
        uint8_t original_selector =
            (uint8_t)((instance->button >> FIAT_V2_BTN_SHIFT) & 0x03U);
        uint8_t display_selector =
            fiat_v2_ui_selector(subghz_custom_btn_get(), original_selector);
        uint8_t display_btn =
            (uint8_t)((instance->button & 0x3FU) | ((display_selector & 0x03U) << FIAT_V2_BTN_SHIFT));
        furi_string_cat_printf(
            output,
            "%s %ubit\r\n"
            "Key:%02X%02X%02X%02X%02X%02X\r\n"
            "SN:0x%lX Btn:[%s]\r\n"
            "Cnt:%02lX\r\n",
            instance->generic.protocol_name,
            FIAT_V2_LOGICAL_BITS,
            instance->hitag2_key[0],
            instance->hitag2_key[1],
            instance->hitag2_key[2],
            instance->hitag2_key[3],
            instance->hitag2_key[4],
            instance->hitag2_key[5],
            (unsigned long)instance->uid,
            fiat_v2_button_name(display_btn),
            (unsigned long)instance->generic.cnt);
    } else {
        furi_string_cat_printf(
            output,
            "%s %ubit\r\n"
            "Key:?\r\n"
            "SN:0x%lX Btn:[%s]\r\n"
            "Cnt:%02lX\r\n",
            instance->generic.protocol_name,
            FIAT_V2_LOGICAL_BITS,
            (unsigned long)instance->uid,
            fiat_v2_button_name(instance->button),
            (unsigned long)instance->generic.cnt);
    }
}

static const SubGhzDecodeEntry fiat_v2_items[] = {{.name = "Fiat V2", .get_string = fiat_v2_decode}};
static const SubGhzDecodeRegistry fiat_v2_registry = {.items = fiat_v2_items, .size = 1};
static const FlipperAppPluginDescriptor decode_fiat_v2_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &fiat_v2_registry};
const FlipperAppPluginDescriptor* decode_fiat_v2_ep(void) { return &decode_fiat_v2_descriptor; }
