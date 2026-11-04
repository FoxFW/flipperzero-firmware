/* decode_phoenix_v2.fal : SD get_string for phoenix_v2 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/phoenix_v2.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>

struct SubGhzProtocolDecoderPhoenix_V2 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static uint16_t subghz_protocol_phoenix_v2_decrypt_counter(uint64_t full_key);
static void subghz_protocol_phoenix_v2_check_remote_controller(SubGhzBlockGeneric* instance);

static uint16_t subghz_protocol_phoenix_v2_decrypt_counter(uint64_t full_key) {
    uint16_t encrypted_value = (uint16_t)((full_key >> 40) & 0xFFFF);

    uint8_t byte1 = (uint8_t)(encrypted_value >> 8);
    uint8_t byte2 = (uint8_t)(encrypted_value & 0xFF);

    uint8_t xor_key1 = (uint8_t)(full_key >> 24);
    uint8_t xor_key2 = (uint8_t)((full_key >> 16) & 0xFF);

    for(int i = 0; i < 16; i++) {

        uint8_t msb_of_byte1 = byte1 & 0x80;

        uint8_t lsb_of_byte2 = byte2 & 1;

        byte2 = (byte2 >> 1) | msb_of_byte1;
        byte1 = (byte1 << 1) | lsb_of_byte2;

        if(msb_of_byte1 == 0) {
            byte1 ^= xor_key1;

            byte2 = (byte2 ^ xor_key2) & 0x7F;
        }
    }

    return (uint16_t)byte2 << 8 | byte1;
}

static void subghz_protocol_phoenix_v2_check_remote_controller(SubGhzBlockGeneric* instance) {

    uint64_t data_rev =
        subghz_protocol_blocks_reverse_key(instance->data, instance->data_count_bit + 4);

    instance->serial = data_rev & 0xFFFFFFFF;
    instance->cnt = subghz_protocol_phoenix_v2_decrypt_counter(data_rev);
    instance->btn = (data_rev >> 32) & 0xF;

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->btn);
    }
    subghz_custom_btn_set_max(4);
}

static void phoenix_v2_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderPhoenix_V2* instance = input->decoder;

    subghz_protocol_phoenix_v2_check_remote_controller(&instance->generic);

    subghz_block_generic_global.cnt_is_available = true;
    subghz_block_generic_global.cnt_length_bit = 16;
    subghz_block_generic_global.current_cnt = instance->generic.cnt;

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 4;

    furi_string_cat_printf(
        output,
        "V2 Phoenix %dbit\r\n"
        "Key:%05lX%08lX\r\n"
        "Sn:0x%07lX \r\n"
        "Cnt:%04lX\r\n"
        "Btn:%X\r\n",
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data >> 32) & 0xFFFFFFFF,
        (uint32_t)(instance->generic.data & 0xFFFFFFFF),
        instance->generic.serial,
        instance->generic.cnt,
        instance->generic.btn);
}

static const SubGhzDecodeEntry phoenix_v2_items[] = {{.name = "Phoenix_V2", .get_string = phoenix_v2_decode}};
static const SubGhzDecodeRegistry phoenix_v2_registry = {.items = phoenix_v2_items, .size = 1};
static const FlipperAppPluginDescriptor decode_phoenix_v2_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &phoenix_v2_registry};
const FlipperAppPluginDescriptor* decode_phoenix_v2_ep(void) { return &decode_phoenix_v2_descriptor; }
