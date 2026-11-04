/* decode_renault_v0.fal : SD get_string for renault_v0 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/renault_v0.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/toolbox/level_duration.h>

#define RENAULT_V0_DECODED_BITS_MAX   0x70U

typedef enum {
    RenaultV0TypeUnknown = 0,
    RenaultV0Type13,
    RenaultV0Type04,
    RenaultV0Type0C,
    RenaultV0Type1A,
    RenaultV0Type3B,
    RenaultV0Type3F,
    RenaultV0TypeDynamic,
} RenaultV0TypeId;

typedef struct {
    uint64_t data;
    uint32_t key2;
    uint32_t serial;
    uint8_t button;
    uint8_t counter;
    RenaultV0TypeId type_id;
    uint8_t type_tag;
    uint8_t preamble_bits;
    bool c1_ok;
    bool c2_ok;
    bool ic_ok;
} RenaultV0DecodeAttempt;

typedef struct SubGhzProtocolDecoderRenaultV0 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t packet_bit_count;
    uint8_t check_c1;
    uint8_t check_c2;
    uint8_t check_ic;

    uint32_t key2;

    uint8_t manchester_state;
    uint8_t decoded_bits[RENAULT_V0_DECODED_BITS_MAX];
    uint8_t decoded_bit_count;
    RenaultV0TypeId type_id;
    uint8_t type_tag;
    uint8_t preamble_bits;
    bool pending_attempt_valid;
    RenaultV0DecodeAttempt pending_attempt;
} SubGhzProtocolDecoderRenaultV0;

static const char* renault_v0_get_button_name(RenaultV0TypeId type_id, uint8_t button);

static const char* renault_v0_get_button_name(RenaultV0TypeId type_id, uint8_t button) {
    if(type_id == RenaultV0Type13) {
        switch(button) {
        case 0x06:
            return "Lock";
        case 0x0A:
            return "Unlock";
        default:
            return "??";
        }
    }

    const uint8_t low_nibble = button & 0x0FU;
    if((low_nibble >= 0x04U) && (low_nibble <= 0x07U)) {
        return "Lock";
    }
    if((low_nibble >= 0x08U) && (low_nibble <= 0x0BU)) {
        return "Unlock";
    }
    return "??";
}

static void renault_v0_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderRenaultV0* instance = input->decoder;


    uint8_t display_btn = (uint8_t)instance->generic.btn;
    if(instance->type_id == RenaultV0Type13) {
        subghz_custom_btn_set_max(4);
        switch(subghz_custom_btn_get()) {
        case SUBGHZ_CUSTOM_BTN_UP:
            display_btn = 0x06U;
            break;
        case SUBGHZ_CUSTOM_BTN_DOWN:
            display_btn = 0x0AU;
            break;
        case SUBGHZ_CUSTOM_BTN_OK:
        case SUBGHZ_CUSTOM_BTN_LEFT:
        case SUBGHZ_CUSTOM_BTN_RIGHT:
        default:
            display_btn = (uint8_t)instance->generic.btn;
            break;
        }
    }

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%016llX\r\n"
        "Key2:%05lX Sn:%06lX\r\n"
        "Btn:%01X [%s] Cnt:%02lX\r\n"
        "C1:[%s] C2:[%s]\r\n"
        "IC:[%s]",
        instance->generic.protocol_name,
        instance->packet_bit_count,
        instance->generic.data,
        instance->key2,
        instance->generic.serial,
        display_btn,
        renault_v0_get_button_name(instance->type_id, display_btn),
        instance->generic.cnt,
        instance->check_c1 ? "ERR" : "OK",
        instance->check_c2 ? "ERR" : "OK",
        instance->check_ic ? "MISS" : "MATCH");
}

static const SubGhzDecodeEntry renault_v0_items[] = {{.name = "Renault V0", .get_string = renault_v0_decode}};
static const SubGhzDecodeRegistry renault_v0_registry = {.items = renault_v0_items, .size = 1};
static const FlipperAppPluginDescriptor decode_renault_v0_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &renault_v0_registry};
const FlipperAppPluginDescriptor* decode_renault_v0_ep(void) { return &decode_renault_v0_descriptor; }
