/* decode_came_twee.fal : SD get_string for came_twee (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/came_twee.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define DIP_PATTERN "%c%c%c%c%c%c%c%c%c%c"
#define CNT_TO_DIP(dip)                                                                     \
    (dip & 0x0200 ? '1' : '0'), (dip & 0x0100 ? '1' : '0'), (dip & 0x0080 ? '1' : '0'),     \
        (dip & 0x0040 ? '1' : '0'), (dip & 0x0020 ? '1' : '0'), (dip & 0x0010 ? '1' : '0'), \
        (dip & 0x0008 ? '1' : '0'), (dip & 0x0004 ? '1' : '0'), (dip & 0x0002 ? '1' : '0'), \
        (dip & 0x0001 ? '1' : '0')

struct SubGhzProtocolDecoderCameTwee {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    ManchesterState manchester_saved_state;
};

static const uint32_t came_twee_magic_numbers_xor[15] = {
    0x0E0E0E00,
    0x1D1D1D11,
    0x2C2C2C22,
    0x3B3B3B33,
    0x4A4A4A44,
    0x59595955,
    0x68686866,
    0x77777777,
    0x86868688,
    0x95959599,
    0xA4A4A4AA,
    0xB3B3B3BB,
    0xC2C2C2CC,
    0xD1D1D1DD,
    0xE0E0E0EE,
};

static void subghz_protocol_came_twee_remote_controller(SubGhzBlockGeneric* instance);

static void subghz_protocol_came_twee_remote_controller(SubGhzBlockGeneric* instance) {

    uint8_t cnt_parcel = (uint8_t)(instance->data & 0xF);
    uint32_t data = (uint32_t)(instance->data & 0x0FFFFFFFF);

    data = (data ^ came_twee_magic_numbers_xor[cnt_parcel]);
    instance->serial = data;
    data /= 4;
    instance->btn = (data >> 4) & 0x0F;
    data >>= 16;
    data = (uint16_t)subghz_protocol_blocks_reverse_key(data, 16);
    instance->cnt = data >> 6;
}

static void came_twee_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderCameTwee* instance = input->decoder;

    subghz_protocol_came_twee_remote_controller(&instance->generic);
    uint32_t code_found_hi = instance->generic.data >> 32;
    uint32_t code_found_lo = instance->generic.data & 0x00000000ffffffff;

    subghz_block_generic_global.btn_is_available = false;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 4;

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "Key:0x%lX%08lX\r\n"
        "Btn:%X\r\n"
        "DIP:" DIP_PATTERN "\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        code_found_hi,
        code_found_lo,
        instance->generic.btn,
        CNT_TO_DIP(instance->generic.cnt));
}

static const SubGhzDecodeEntry came_twee_items[] = {{.name = "CAME TWEE", .get_string = came_twee_decode}};
static const SubGhzDecodeRegistry came_twee_registry = {.items = came_twee_items, .size = 1};
static const FlipperAppPluginDescriptor decode_came_twee_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &came_twee_registry};
const FlipperAppPluginDescriptor* decode_came_twee_ep(void) { return &decode_came_twee_descriptor; }
