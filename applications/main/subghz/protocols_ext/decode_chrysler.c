/* decode_chrysler.fal : SD get_string for chrysler (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/chrysler.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <string.h>

struct SubGhzProtocolDecoderChrysler {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t packet_bit_count;
    uint8_t decoded_button;

    uint32_t te_last;
    uint8_t plain_a[9];
    uint8_t plain_b[9];

    uint8_t plain_a_present;
    uint8_t plain_b_present;

    uint8_t check_ok;
    uint32_t sn_b;

    uint16_t data_2;
    uint8_t seed;
};

static const char* chrysler_v0_get_button_name(uint8_t button);
static uint32_t chrysler_v0_get_sn_b(const SubGhzProtocolDecoderChrysler* instance);

static const char* chrysler_v0_get_button_name(uint8_t button) {
    switch(button) {
    case 1:
        return "Lock";
    case 2:
        return "Unlock";
    default:
        return "??";
    }
}

static uint32_t chrysler_v0_get_sn_b(const SubGhzProtocolDecoderChrysler* instance) {
    return instance->sn_b;
}

static void chrysler_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderChrysler* instance = input->decoder;


    furi_string_cat_printf(
        output,
        "%s %dbit\r\n%016llX%04X\r\n",
        instance->generic.protocol_name,
        instance->packet_bit_count,
        instance->generic.data,
        instance->data_2);

    if(instance->plain_a_present) {
        if(instance->plain_b_present) {
            furi_string_cat_printf(
                output,
                "SnA:%08lX\r\nSnB:%08lX\r\n",
                instance->generic.cnt,
                chrysler_v0_get_sn_b(instance));
        } else {
            furi_string_cat_printf(output, "SnA:%08lX\r\n", instance->generic.cnt);
        }
    } else if(instance->plain_b_present) {
        furi_string_cat_printf(output, "SnB:%08lX\r\n", chrysler_v0_get_sn_b(instance));
    }

    subghz_custom_btn_set_max(2);
    uint8_t display_btn = instance->decoded_button;
    switch(subghz_custom_btn_get()) {
    case SUBGHZ_CUSTOM_BTN_UP:
        display_btn = 0x1U;
        break;
    case SUBGHZ_CUSTOM_BTN_DOWN:
        display_btn = 0x2U;
        break;
    case SUBGHZ_CUSTOM_BTN_OK:
    default:
        break;
    }

    furi_string_cat_printf(
        output,
        "Btn:%02X [%s] Cnt:%02X\r\nChk:%s",
        display_btn,
        chrysler_v0_get_button_name(display_btn),
        instance->seed,
        instance->check_ok ? "OK" : "ERR");
}

static const SubGhzDecodeEntry chrysler_items[] = {{.name = "Chrysler", .get_string = chrysler_decode}};
static const SubGhzDecodeRegistry chrysler_registry = {.items = chrysler_items, .size = 1};
static const FlipperAppPluginDescriptor decode_chrysler_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &chrysler_registry};
const FlipperAppPluginDescriptor* decode_chrysler_ep(void) { return &decode_chrysler_descriptor; }
