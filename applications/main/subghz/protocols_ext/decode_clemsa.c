/* decode_clemsa.fal : SD get_string for clemsa (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/clemsa.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define DIP_P 0b11
#define DIP_O 0b10
#define DIP_N 0b00
#define DIP_PATTERN "%c%c%c%c%c%c%c%c"
#define SHOW_DIP_P(dip, check_dip)                         \
    ((((dip >> 0xE) & 0x3) == check_dip) ? '*' : '_'),     \
        ((((dip >> 0xC) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0xA) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x8) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x6) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x4) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x2) & 0x3) == check_dip) ? '*' : '_'), \
        ((((dip >> 0x0) & 0x3) == check_dip) ? '*' : '_')

struct SubGhzProtocolDecoderClemsa {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static void subghz_protocol_clemsa_check_remote_controller(SubGhzBlockGeneric* instance);

static void subghz_protocol_clemsa_check_remote_controller(SubGhzBlockGeneric* instance) {
    instance->serial = (instance->data >> 2) & 0xFFFF;
    instance->btn = (instance->data & 0x03);
}

static void clemsa_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderClemsa* instance = input->decoder;

    subghz_protocol_clemsa_check_remote_controller(&instance->generic);

    subghz_block_generic_global.btn_is_available = false;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 2;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%05lX   Btn:%X\r\n"
        "  +:   " DIP_PATTERN "\r\n"
        "  o:   " DIP_PATTERN "\r\n"
        "  -:   " DIP_PATTERN "\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data & 0x3FFFF),
        instance->generic.btn,
        SHOW_DIP_P(instance->generic.serial, DIP_P),
        SHOW_DIP_P(instance->generic.serial, DIP_O),
        SHOW_DIP_P(instance->generic.serial, DIP_N));
}

static const SubGhzDecodeEntry clemsa_items[] = {{.name = "Clemsa", .get_string = clemsa_decode}};
static const SubGhzDecodeRegistry clemsa_registry = {.items = clemsa_items, .size = 1};
static const FlipperAppPluginDescriptor decode_clemsa_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &clemsa_registry};
const FlipperAppPluginDescriptor* decode_clemsa_ep(void) { return &decode_clemsa_descriptor; }
