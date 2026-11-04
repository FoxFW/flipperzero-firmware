/* decode_dooya.fal : SD get_string for dooya (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/dooya.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define DOYA_SINGLE_CHANNEL 0xFF

struct SubGhzProtocolDecoderDooya {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static void subghz_protocol_dooya_check_remote_controller(SubGhzBlockGeneric* instance);
static const char* subghz_protocol_dooya_get_name_button(uint8_t btn);

static void subghz_protocol_dooya_check_remote_controller(SubGhzBlockGeneric* instance) {

    instance->serial = (instance->data >> 16);
    if((instance->data >> 12) & 0x0F) {
        instance->cnt = (instance->data >> 8) & 0x0F;
    } else {
        instance->cnt = DOYA_SINGLE_CHANNEL;
    }
    instance->btn = instance->data & 0xFF;
}

static const char* subghz_protocol_dooya_get_name_button(uint8_t btn) {
    const char* btn_name;
    switch(btn) {
    case 0b00010001:
        btn_name = "Up_Long";
        break;
    case 0b00011110:
        btn_name = "Up_Short";
        break;
    case 0b00110011:
        btn_name = "Down_Long";
        break;
    case 0b00111100:
        btn_name = "Down_Short";
        break;
    case 0b01010101:
        btn_name = "Stop";
        break;
    case 0b01111001:
        btn_name = "Up+Down";
        break;
    case 0b10000000:
        btn_name = "Up+Stop";
        break;
    case 0b10000001:
        btn_name = "Down+Stop";
        break;
    case 0b11001100:
        btn_name = "P2";
        break;
    default:
        btn_name = "Unknown";
        break;
    }
    return btn_name;
}

static void dooya_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderDooya* instance = input->decoder;


    subghz_protocol_dooya_check_remote_controller(&instance->generic);

    subghz_block_generic_global.btn_is_available = false;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 8;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%010llX\r\n"
        "Sn:0x%08lX\r\n"
        "Btn:%X - %s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        instance->generic.data,
        instance->generic.serial,
        instance->generic.btn,
        subghz_protocol_dooya_get_name_button(instance->generic.btn));
    if(instance->generic.cnt == DOYA_SINGLE_CHANNEL) {
        furi_string_cat_printf(output, "Ch:Single\r\n");
    } else {
        furi_string_cat_printf(output, "Ch:%lu\r\n", instance->generic.cnt);
    }
}

static const SubGhzDecodeEntry dooya_items[] = {{.name = "Dooya", .get_string = dooya_decode}};
static const SubGhzDecodeRegistry dooya_registry = {.items = dooya_items, .size = 1};
static const FlipperAppPluginDescriptor decode_dooya_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &dooya_registry};
const FlipperAppPluginDescriptor* decode_dooya_ep(void) { return &decode_dooya_descriptor; }
