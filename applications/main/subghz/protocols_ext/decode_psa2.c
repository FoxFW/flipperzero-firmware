/* decode_psa2.fal : SD get_string for psa2 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/psa2.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/toolbox/manchester_decoder.h>

#define PSA_BTN_LOCK    0x0
#define PSA_BTN_UNLOCK  0x1
#define PSA_BTN_TRUNK   0x2
#define PSA_MODE_23       0x23

struct SubGhzProtocolDecoderPSA {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t state;
    uint32_t prev_duration;

    uint32_t decode_data_low;
    uint32_t decode_data_high;
    uint8_t  decode_count_bit;

    uint32_t key1_low;
    uint32_t key1_high;
    uint16_t validation_field;
    uint32_t key2_low;
    uint32_t key2_high;

    uint32_t status_flag;

    uint8_t  mode_serialize;

    uint16_t decrypted;

    uint8_t  decrypted_button;
    uint32_t decrypted_serial;
    uint32_t decrypted_counter;
    uint16_t decrypted_crc;
    uint32_t decrypted_seed;
    uint8_t  decrypted_type;

    uint16_t       pattern_counter;
    ManchesterState manchester_state;

    uint32_t last_key1_low;
    uint32_t last_key1_high;

    uint8_t bf_attempted;
};

static const char* psa_button_name(uint8_t btn);
static uint8_t psa_get_btn_code(void);

static const char* psa_button_name(uint8_t btn) {
    switch(btn) {
    case PSA_BTN_LOCK:   return "Lock";
    case PSA_BTN_UNLOCK: return "Unlock";
    case PSA_BTN_TRUNK:  return "Trunk";
    default:             return "??";
    }
}

static uint8_t psa_get_btn_code(void) {
    uint8_t custom_btn   = subghz_custom_btn_get();
    uint8_t original_raw = subghz_custom_btn_get_original();
    uint8_t original_btn = (original_raw == 0xFF) ? PSA_BTN_LOCK : original_raw;
    if(custom_btn == SUBGHZ_CUSTOM_BTN_OK)    return original_btn;
    if(custom_btn == SUBGHZ_CUSTOM_BTN_UP)    return PSA_BTN_LOCK;
    if(custom_btn == SUBGHZ_CUSTOM_BTN_DOWN)  return PSA_BTN_UNLOCK;
    if(custom_btn == SUBGHZ_CUSTOM_BTN_LEFT)  return PSA_BTN_TRUNK;
    if(custom_btn == SUBGHZ_CUSTOM_BTN_RIGHT) return PSA_BTN_TRUNK;
    return original_btn;
}

static void psa2_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderPSA* inst = input->decoder;


    uint16_t key2_val = (uint16_t)(inst->key2_low & 0xFFFF);

    if(inst->decrypted == 0x50 && inst->decrypted_type != 0) {
        subghz_custom_btn_set_original(inst->generic.btn == 0 ? 0xFF : inst->generic.btn);
        subghz_custom_btn_set_max(4);
        uint8_t display_btn = psa_get_btn_code();

        if(inst->decrypted_type == PSA_MODE_23) {
            furi_string_printf(output,
                "%s %dbit\r\n"
                "Key1:%08lX%08lX\r\n"
                "Key2:%04X Ser:%06lX\r\n"
                "Btn:[%s] Cnt:%04lX\r\n"
                "Type:%02X Sd:%06lX CRC:%02X",
                inst->base.protocol->name, 128,
                inst->key1_high, inst->key1_low,
                key2_val, inst->generic.serial,
                psa_button_name(display_btn), inst->generic.cnt,
                inst->decrypted_type, inst->decrypted_seed,
                inst->decrypted_crc);
        } else {
            furi_string_printf(output,
                "%s %dbit\r\n"
                "Key1:%08lX%08lX\r\n"
                "Key2:%04X Ser:%06lX\r\n"
                "Btn:[%s] Cnt:%08lX\r\n"
                "Type:%02X Sd:%06lX CRC:%04X",
                inst->base.protocol->name, 128,
                inst->key1_high, inst->key1_low,
                key2_val, inst->generic.serial,
                psa_button_name(display_btn), inst->generic.cnt,
                inst->decrypted_type, inst->decrypted_seed,
                inst->decrypted_crc);
        }
    } else {
        furi_string_printf(output,
            "%s %dbit\r\n"
            "Key1:%08lX%08lX\r\n"
            "Key2:%04X",
            inst->base.protocol->name, 128,
            inst->key1_high, inst->key1_low,
            key2_val);
    }
}

static const SubGhzDecodeEntry psa2_items[] = {{.name = "PSA OLD", .get_string = psa2_decode}};
static const SubGhzDecodeRegistry psa2_registry = {.items = psa2_items, .size = 1};
static const FlipperAppPluginDescriptor decode_psa2_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &psa2_registry};
const FlipperAppPluginDescriptor* decode_psa2_ep(void) { return &decode_psa2_descriptor; }
