/* decode_secplus_v1.fal : SD get_string for secplus_v1 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/secplus_v1.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderSecPlus_v1 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t packet_accepted;
    uint8_t base_packet_index;
    uint8_t data_array[44];
};

static void secplus_v1_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderSecPlus_v1* instance = input->decoder;


    uint32_t fixed = (instance->generic.data >> 32) & 0xFFFFFFFF;
    instance->generic.cnt = instance->generic.data & 0xFFFFFFFF;

    instance->generic.btn = fixed % 3;
    uint8_t id0 = (fixed / 3) % 3;
    uint8_t id1 = (fixed / 9) % 3;
    uint16_t pin = 0;

    subghz_block_generic_global.cnt_is_available = true;
    subghz_block_generic_global.cnt_length_bit = 32;
    subghz_block_generic_global.current_cnt = instance->generic.cnt;

    subghz_block_generic_global.btn_is_available = false;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 2;

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "Key:%lX%08lX\r\n"
        "id1:%d id0:%d",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data >> 32),
        (uint32_t)instance->generic.data,
        id1,
        id0);

    if(id1 == 0) {

        instance->generic.serial = (fixed / 27) % 2187;

        pin = (fixed / 59049) % 19683;

        if(pin <= 9999) {
            furi_string_cat_printf(output, " pin:%d", pin);
        } else if(pin <= 11029) {
            furi_string_cat_printf(output, " pin:enter");
        }

        int pin_suffix = 0;

        pin_suffix = (fixed / 1162261467) % 3;

        if(pin_suffix == 1) {
            furi_string_cat_printf(output, " #\r\n");
        } else if(pin_suffix == 2) {
            furi_string_cat_printf(output, " *\r\n");
        } else {
            furi_string_cat_printf(output, "\r\n");
        }

        furi_string_cat_printf(
            output,
            "Sn:0x%08lX\r\n"
            "Cnt:%08lX "
            "SwID:0x%X\r\n",
            instance->generic.serial,
            instance->generic.cnt,
            instance->generic.btn);
    } else {

        instance->generic.serial = fixed / 27;
        if(instance->generic.btn == 1) {
            furi_string_cat_printf(output, " Btn:left\r\n");
        } else if(instance->generic.btn == 0) {
            furi_string_cat_printf(output, " Btn:middle\r\n");
        } else if(instance->generic.btn == 2) {
            furi_string_cat_printf(output, " Btn:right\r\n");
        }

        furi_string_cat_printf(
            output,
            "Sn:0x%08lX\r\n"
            "Cnt:%08lX "
            "SwID:0x%X\r\n",
            instance->generic.serial,
            instance->generic.cnt,
            instance->generic.btn);
    }
}

static const SubGhzDecodeEntry secplus_v1_items[] = {{.name = "Security+ 1.0", .get_string = secplus_v1_decode}};
static const SubGhzDecodeRegistry secplus_v1_registry = {.items = secplus_v1_items, .size = 1};
static const FlipperAppPluginDescriptor decode_secplus_v1_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &secplus_v1_registry};
const FlipperAppPluginDescriptor* decode_secplus_v1_ep(void) { return &decode_secplus_v1_descriptor; }
