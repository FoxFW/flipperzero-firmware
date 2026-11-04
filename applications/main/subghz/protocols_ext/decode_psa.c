/* decode_psa.fal : SD get_string for psa (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/psa.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/toolbox/manchester_decoder.h>

#define PSA_AM_LONG_MIN_PCT 4U

struct SubGhzProtocolDecoderPSA {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t state;
    uint32_t prev_duration;

    uint32_t decode_data_low;
    uint32_t decode_data_high;
    uint8_t decode_count_bit;

    uint32_t key1_low;
    uint32_t key1_high;
    uint16_t validation_field;
    uint32_t key2_low;
    uint32_t key2_high;

    uint32_t status_flag;
    uint16_t decrypted;
    uint8_t mode_serialize;

    uint8_t decrypted_button;
    uint32_t decrypted_serial;
    uint32_t decrypted_counter;
    uint16_t decrypted_crc;
    uint32_t decrypted_seed;
    uint8_t decrypted_type;

    uint16_t pattern_counter;
    ManchesterState manchester_state;

    uint32_t last_key1_low;
    uint32_t last_key1_high;

    uint32_t te_sum;
    uint16_t te_count;
    uint32_t te_detected;

    uint8_t pre_glitch;
    uint8_t am_await_high;
    uint8_t am_bits[96];
    uint8_t am_bits_len;
    uint16_t am_long_count;
    uint16_t am_pulse_count;
};

static const char* psa_button_name(uint8_t btn);
static uint8_t psa_btn_to_custom(uint8_t btn);
static uint8_t psa_custom_to_btn(uint8_t custom_btn, uint8_t fallback_btn);
static uint8_t psa_get_btn_code(void);

static const char* psa_button_name(uint8_t btn) {
    switch(btn) {
    case 0x0:
        return "Lock";
    case 0x1:
        return "Unlock";
    case 0x2:
        return "Trunk";
    default:
        return "??";
    }
}

static uint8_t psa_btn_to_custom(uint8_t btn) {
    switch(btn) {
    case 0x0:
        return SUBGHZ_CUSTOM_BTN_UP;
    case 0x1:
        return SUBGHZ_CUSTOM_BTN_DOWN;
    case 0x2:
        return SUBGHZ_CUSTOM_BTN_LEFT;
    default:
        return SUBGHZ_CUSTOM_BTN_OK;
    }
}

static uint8_t psa_custom_to_btn(uint8_t custom_btn, uint8_t fallback_btn) {
    switch(custom_btn) {
    case SUBGHZ_CUSTOM_BTN_UP:
        return 0x0;
    case SUBGHZ_CUSTOM_BTN_DOWN:
        return 0x1;
    case SUBGHZ_CUSTOM_BTN_LEFT:
    case SUBGHZ_CUSTOM_BTN_RIGHT:
        return 0x2;
    default:
        return fallback_btn;
    }
}

static uint8_t psa_get_btn_code(void) {
    uint8_t custom_btn = subghz_custom_btn_get();
    uint8_t original_btn = psa_custom_to_btn(subghz_custom_btn_get_original(), 0x0);
    if(custom_btn == SUBGHZ_CUSTOM_BTN_OK) return original_btn;
    return psa_custom_to_btn(custom_btn, original_btn);
}

static void psa_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderPSA* instance = input->decoder;


    uint16_t key2_value = (uint16_t)(instance->key2_low & 0xFFFF);

    if(instance->decrypted == 0x50 && instance->decrypted_type != 0) {

        subghz_custom_btn_set_original(psa_btn_to_custom(instance->generic.btn));
        subghz_custom_btn_set_max(4);
        uint8_t display_btn = psa_get_btn_code();
        if(instance->decrypted_type == 0x23) {
            furi_string_printf(
                output,
                "%s %dbit\r\n"
                "Key1:%08lX%08lX\r\n"
                "Key2:%04X Ser:%06lX\r\n"
                "Btn:[%s] Cnt:%04lX\r\n"
                "Type:%02X Sd:%06lX CRC:%02X",
                instance->base.protocol->name,
                128,
                instance->key1_high,
                instance->key1_low,
                key2_value,
                instance->generic.serial,
                psa_button_name(display_btn),
                instance->generic.cnt,
                instance->decrypted_type,
                instance->decrypted_seed,
                instance->decrypted_crc);
        } else if(instance->decrypted_type == 0x36) {
            furi_string_printf(
                output,
                "%s %dbit\r\n"
                "Key1:%08lX%08lX\r\n"
                "Key2:%04X Ser:%06lX\r\n"
                "Btn:[%s] Cnt:%08lX\r\n"
                "Type:%02X Sd:%06lX CRC:%02X",
                instance->base.protocol->name,
                128,
                instance->key1_high,
                instance->key1_low,
                key2_value,
                instance->generic.serial,
                psa_button_name(display_btn),
                instance->generic.cnt,
                instance->decrypted_type,
                instance->decrypted_seed,
                instance->decrypted_crc);
        }
    } else {
        furi_string_printf(
            output,
            "%s %dbit\r\n"
            "Key1:%08lX%08lX\r\n"
            "Key2:%X",
            "Unknown Signal",
            128,
            instance->key1_high,
            instance->key1_low,
            key2_value);
        if(instance->am_pulse_count > 0) {
            bool psa_like = (uint32_t)instance->am_long_count * 100U >=
                            (uint32_t)instance->am_pulse_count * PSA_AM_LONG_MIN_PCT;
            furi_string_cat_printf(
                output, "\r\nTiming:%s", psa_like ? "PSA-typ" : "fast-OOK");
        }
    }
}

static const SubGhzDecodeEntry psa_items[] = {{.name = "PSA GROUP", .get_string = psa_decode}};
static const SubGhzDecodeRegistry psa_registry = {.items = psa_items, .size = 1};
static const FlipperAppPluginDescriptor decode_psa_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &psa_registry};
const FlipperAppPluginDescriptor* decode_psa_ep(void) { return &decode_psa_descriptor; }
