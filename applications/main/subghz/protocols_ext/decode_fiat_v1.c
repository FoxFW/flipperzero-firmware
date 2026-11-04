/* decode_fiat_v1.fal : SD get_string for fiat_v1 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/blocks/bitbuf.h>
#include <lib/subghz/protocols/fiat_v1.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <string.h>

#define FIAT_V1_WIRE_BITS         104U
#define FIAT_V1_WIRE_BYTES        13U
#define FIAT_V1_WIRE_CELLS        (FIAT_V1_WIRE_BITS * 2U)
#define FIAT_V1_LOGICAL_BITS      102U
#define FIAT_V1_VARIANT_COUNT     2U

struct SubGhzProtocolDecoderFiatV1 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t cells[FIAT_V1_VARIANT_COUNT][SUBGHZ_BITBUF_BYTES(FIAT_V1_WIRE_CELLS)];
    uint16_t cell_count[FIAT_V1_VARIANT_COUNT];

    uint8_t raw_data[FIAT_V1_WIRE_BYTES];
    uint8_t last_raw_data[FIAT_V1_WIRE_BYTES];
    bool last_raw_valid;

    uint32_t uid;
    uint32_t hop;
    uint8_t family;
    uint8_t tail_bits;
    uint8_t frame_xor;

    uint8_t hitag2_key[6];
    uint32_t hitag2_epoch;
    bool hitag2_key_valid;
};

static const char* fiat_v1_button_name(uint8_t button);
static uint8_t fiat_v1_custom_to_btn(uint8_t custom, uint8_t original_btn);

static const char* fiat_v1_button_name(uint8_t button) {
    switch(button) {
    case 0x8U:
        return "Unlock";
    case 0x4U:
        return "Lock";
    case 0x2U:
        return "Trunk";
    case 0x1U:
        return "Close";
    default:
        return "Unknown";
    }
}

static uint8_t fiat_v1_custom_to_btn(uint8_t custom, uint8_t original_btn) {
    switch(custom) {
    case SUBGHZ_CUSTOM_BTN_UP:
        return 0x8U;
    case SUBGHZ_CUSTOM_BTN_DOWN:
        return 0x4U;
    case SUBGHZ_CUSTOM_BTN_LEFT:
        return 0x2U;
    case SUBGHZ_CUSTOM_BTN_RIGHT:
        return 0x1U;
    case SUBGHZ_CUSTOM_BTN_OK:
    default:
        return original_btn;
    }
}

static void fiat_v1_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderFiatV1* instance = input->decoder;


    uint8_t display_btn = instance->generic.btn;
    if(instance->hitag2_key_valid) {
        subghz_custom_btn_set_max(4);
        uint8_t custom = subghz_custom_btn_get();
        if(custom != SUBGHZ_CUSTOM_BTN_OK) {
            display_btn = fiat_v1_custom_to_btn(custom, instance->generic.btn);
        }
    }

    furi_string_cat_printf(
        output,
        "%s %ubit\r\n"
        "%s\r\n"
        "%08lX %03lX%01X %08lX\r\n"
        "Sync:%02X UID:%08lX\r\n"
        "Btn:%02X [%s] Ctrl:%03lX\r\n"
        "Tail:%u XOR:%02X\r\n"
        "Auth:%08lX\r\n",
        instance->generic.protocol_name,
        FIAT_V1_LOGICAL_BITS,
        instance->hitag2_key_valid ? "KEY:OK" : "KEY:??",
        (unsigned long)instance->generic.serial,
        (unsigned long)instance->generic.cnt,
        display_btn,
        (unsigned long)instance->hop,
        instance->family,
        (unsigned long)instance->uid,
        display_btn,
        fiat_v1_button_name(display_btn),
        (unsigned long)instance->generic.cnt,
        instance->tail_bits,
        instance->frame_xor,
        (unsigned long)instance->hop);
}

static const SubGhzDecodeEntry fiat_v1_items[] = {{.name = "Fiat V1", .get_string = fiat_v1_decode}};
static const SubGhzDecodeRegistry fiat_v1_registry = {.items = fiat_v1_items, .size = 1};
static const FlipperAppPluginDescriptor decode_fiat_v1_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &fiat_v1_registry};
const FlipperAppPluginDescriptor* decode_fiat_v1_ep(void) { return &decode_fiat_v1_descriptor; }
