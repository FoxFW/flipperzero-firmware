/* decode_toyota.fal : SD get_string for toyota (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/toyota.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define TOYOTA_A_BTN_LOCK    0x08
#define TOYOTA_A_BTN_UNLOCK  0x01
#define TOYOTA_B_BTN_LOCK    0x0A
#define TOYOTA_B_BTN_UNLOCK  0x05
#define TOYOTA_C_BTN_LOCK    0x0B
#define TOYOTA_C_BTN_UNLOCK  0x0E
#define TOYOTA_C_BTN_TRUNK   0x0D
#define TOYOTA_C_BTN_PANIC   0x07

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder        decoder;
    SubGhzBlockGeneric        generic;

    uint64_t bits_lo;
    uint8_t  bits_hi;
    uint8_t  bit_count;

    uint32_t te_last;
    bool     have_high;
    uint16_t preamble_count;

    uint8_t  variant;

    uint32_t hop;
    uint32_t serial;
    uint8_t  button;

} SubGhzProtocolDecoderToyota;

static const char* toyota_button_name(uint8_t btn, uint8_t variant);
static const char* toyota_model_name(uint8_t variant);

static const char* toyota_button_name(uint8_t btn, uint8_t variant) {
    if(variant == 2) {
        switch(btn & 0x0F) {
        case TOYOTA_C_BTN_LOCK:   return "Lock";
        case TOYOTA_C_BTN_UNLOCK: return "Unlock";
        case TOYOTA_C_BTN_TRUNK:  return "Trunk";
        case TOYOTA_C_BTN_PANIC:  return "Panic";
        default:                  return "Unknown";
        }
    }
    if(variant == 1) {
        switch(btn & 0x0F) {
        case TOYOTA_B_BTN_LOCK:   return "Lock";
        case TOYOTA_B_BTN_UNLOCK: return "Unlock";
        case 0x0F:                return "Lock+Unlock";
        case 0x04:                return "Trunk";
        default:                  return "Unknown";
        }
    }
    switch(btn & 0x0F) {
    case TOYOTA_A_BTN_LOCK:   return "Lock";
    case TOYOTA_A_BTN_UNLOCK: return "Unlock";
    case 0x09:                return "Lock+Unlock";
    case 0x02:                return "Trunk";
    case 0x04:                return "Aux";
    default:                  return "Unknown";
    }
}

static const char* toyota_model_name(uint8_t variant) {
    if(variant == 2) return "Prius";
    return (variant == 1) ? "Tundra" : "Corolla";
}

static void toyota_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderToyota* inst = input->decoder;


    uint32_t hop    = (uint32_t)(inst->generic.data >> 32);
    uint32_t serial = (uint32_t)((inst->generic.data >> 4) & 0x0FFFFFFF);
    uint8_t  button = (uint8_t)(inst->generic.data & 0x0F);
    uint8_t  var    = (inst->generic.cnt == 2) ? 2 :
                      ((inst->generic.cnt != 0) ? 1 : 0);

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Hop: %08lX\r\n"
        "Sn:  %07lX\r\n"
        "Btn: %X [%s]",
        toyota_model_name(var),
        inst->generic.data_count_bit,
        (unsigned long)hop,
        (unsigned long)serial,
        button,
        toyota_button_name(button, var));
}

static const SubGhzDecodeEntry toyota_items[] = {{.name = "Toyota", .get_string = toyota_decode}};
static const SubGhzDecodeRegistry toyota_registry = {.items = toyota_items, .size = 1};
static const FlipperAppPluginDescriptor decode_toyota_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &toyota_registry};
const FlipperAppPluginDescriptor* decode_toyota_ep(void) { return &decode_toyota_descriptor; }
