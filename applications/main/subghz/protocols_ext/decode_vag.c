/* decode_vag.fal : SD get_string for vag (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/vag.h>
#include <lib/subghz/protocols/aut64.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/toolbox/manchester_decoder.h>
#include <string.h>

typedef struct SubGhzProtocolDecoderVAG {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t data_low;
    uint32_t data_high;
    uint8_t bit_count;
    uint32_t key1_low;
    uint32_t key1_high;
    uint32_t key2_low;
    uint32_t key2_high;
    uint16_t data_count_bit;
    uint8_t vag_type;
    uint16_t header_count;
    uint8_t mid_count;
    ManchesterState manchester_state;

    uint32_t serial;
    uint32_t cnt;
    uint8_t btn;
    uint8_t check_byte;
    uint8_t key_idx;
    bool decrypted;

    uint32_t last_valid_serial;
    uint32_t last_valid_cnt;
} SubGhzProtocolDecoderVAG;

static const char* vag_button_name(uint8_t btn);

static const char* vag_button_name(uint8_t btn) {
    switch(btn) {
    case 0x1:
        return "Unlock";
    case 0x2:
        return "Lock";
    case 0x4:
        return "Boot";
    case 0x10:
        return "Unlock";
    case 0x20:
        return "Lock";
    case 0x40:
        return "Boot";
    default:
        return "Unkn";
    }
}

static void vag_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderVAG* instance = input->decoder;


    /* AUT64 decrypt done internally */

    uint64_t key1 = ((uint64_t)instance->key1_high << 32) | instance->key1_low;
    uint16_t key2 = (uint16_t)(instance->key2_low & 0xFFFF);

    uint8_t type_byte = (uint8_t)(instance->key1_high >> 24);
    const char* vehicle_name;
    switch(type_byte) {
    case 0x00:
        vehicle_name = "VAG NEW";
        break;
    case 0xC0:
        vehicle_name = "VAG OLD";
        break;
    case 0xC1:
        vehicle_name = "AUDI";
        break;
    case 0xC2:
        vehicle_name = "SEAT";
        break;
    case 0xC3:
        vehicle_name = "SKODA";
        break;
    default:
        vehicle_name = "VAG GEN";
        break;
    }

    if(instance->decrypted) {
        furi_string_cat_printf(
            output,
            "%s %db\r\n"
            "Key1:%08lX%08lX\r\n"
            "Key2:%04X KeyIdx:%d\r\n"
            "Ser:%08lX Cnt:%06lX\r\n"
            "Btn:[%s]",
            vehicle_name,
            instance->data_count_bit,
            (unsigned long)(key1 >> 32),
            (unsigned long)(key1 & 0xFFFFFFFF),
            key2,
            instance->key_idx,
            (unsigned long)instance->serial,
            (unsigned long)instance->cnt,
            vag_button_name(instance->btn));
    } else {
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key1:%08lX%08lX\r\n"
            "Key2:%04X (corrupted)\r\n",
            vehicle_name,
            instance->data_count_bit,
            (unsigned long)(key1 >> 32),
            (unsigned long)(key1 & 0xFFFFFFFF),
            key2);
    }
}

static const SubGhzDecodeEntry vag_items[] = {{.name = "VAG GROUP", .get_string = vag_decode}};
static const SubGhzDecodeRegistry vag_registry = {.items = vag_items, .size = 1};
static const FlipperAppPluginDescriptor decode_vag_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &vag_registry};
const FlipperAppPluginDescriptor* decode_vag_ep(void) { return &decode_vag_descriptor; }
