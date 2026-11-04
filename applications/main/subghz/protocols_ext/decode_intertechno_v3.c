/* decode_intertechno_v3.fal : SD get_string for intertechno_v3 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/intertechno_v3.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define CH_PATTERN "%c%c%c%c"
#define CNT_TO_CH(ch) \
    (ch & 0x8 ? '1' : '0'), (ch & 0x4 ? '1' : '0'), (ch & 0x2 ? '1' : '0'), (ch & 0x1 ? '1' : '0')
#define INTERTECHNO_V3_DIMMING_COUNT_BIT 36

struct SubGhzProtocolDecoderIntertechno_V3 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_intertechno_v3_const = {
    .te_short = 275,
    .te_long = 1375,
    .te_delta = 150,
    .min_count_bit_for_found = 32,
};

static void subghz_protocol_intertechno_v3_check_remote_controller(SubGhzBlockGeneric* instance);

static void subghz_protocol_intertechno_v3_check_remote_controller(SubGhzBlockGeneric* instance) {

    if(instance->data_count_bit == subghz_protocol_intertechno_v3_const.min_count_bit_for_found) {
        instance->serial = (instance->data >> 6) & 0x3FFFFFF;
        if((instance->data >> 5) & 0x1) {
            instance->cnt = 1 << 5;
        } else {
            instance->cnt = (~instance->data & 0xF);
        }
        instance->btn = (instance->data >> 4) & 0x1;
    } else if(instance->data_count_bit == INTERTECHNO_V3_DIMMING_COUNT_BIT) {
        instance->serial = (instance->data >> 10) & 0x3FFFFFF;
        if((instance->data >> 9) & 0x1) {
            instance->cnt = 1 << 5;
        } else {
            instance->cnt = (~(instance->data >> 4) & 0xF);
        }
        instance->btn = (instance->data) & 0xF;
    } else {
        instance->serial = 0;
        instance->cnt = 0;
        instance->btn = 0;
    }
}

static void intertechno_v3_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderIntertechno_V3* instance = input->decoder;


    subghz_protocol_intertechno_v3_check_remote_controller(&instance->generic);

    subghz_block_generic_global.current_btn = instance->generic.btn;

    furi_string_cat_printf(
        output,
        "%.11s %db\r\n"
        "Key:0x%08llX\r\n"
        "Sn:%07lX\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        instance->generic.data,
        instance->generic.serial);

    if(instance->generic.data_count_bit ==
       subghz_protocol_intertechno_v3_const.min_count_bit_for_found) {
        if(instance->generic.cnt >> 5) {
            furi_string_cat_printf(
                output, "Ch: All Btn:%s\r\n", (instance->generic.btn ? "On" : "Off"));
            subghz_block_generic_global.btn_is_available = false;
            subghz_block_generic_global.btn_length_bit = 1;
        } else {
            furi_string_cat_printf(
                output,
                "Ch:" CH_PATTERN " Btn:%s\r\n",
                CNT_TO_CH(instance->generic.cnt),
                (instance->generic.btn ? "On" : "Off"));
            subghz_block_generic_global.btn_is_available = false;
            subghz_block_generic_global.btn_length_bit = 1;
        }
    } else if(instance->generic.data_count_bit == INTERTECHNO_V3_DIMMING_COUNT_BIT) {
        furi_string_cat_printf(
            output,
            "Ch:" CH_PATTERN " Dimm:%d%%\r\n",
            CNT_TO_CH(instance->generic.cnt),
            (int)(6.67f * (float)instance->generic.btn));
        subghz_block_generic_global.btn_is_available = false;
        subghz_block_generic_global.btn_length_bit = 4;
    }
}

static const SubGhzDecodeEntry intertechno_v3_items[] = {{.name = "Intertechno_V3", .get_string = intertechno_v3_decode}};
static const SubGhzDecodeRegistry intertechno_v3_registry = {.items = intertechno_v3_items, .size = 1};
static const FlipperAppPluginDescriptor decode_intertechno_v3_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &intertechno_v3_registry};
const FlipperAppPluginDescriptor* decode_intertechno_v3_ep(void) { return &decode_intertechno_v3_descriptor; }
