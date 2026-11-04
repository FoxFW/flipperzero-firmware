/* decode_somfy_keytis.fal : SD get_string for somfy_keytis (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/somfy_keytis.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>

struct SubGhzProtocolDecoderSomfyKeytis {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t header_count;
    ManchesterState manchester_saved_state;
    uint32_t press_duration_counter;
};

static void subghz_protocol_somfy_keytis_check_remote_controller(SubGhzBlockGeneric* instance);
static const char* subghz_protocol_somfy_keytis_get_name_button(uint8_t btn);

static void subghz_protocol_somfy_keytis_check_remote_controller(SubGhzBlockGeneric* instance) {

    uint64_t data = instance->data ^ (instance->data >> 8);
    instance->btn = (data >> 48) & 0xF;
    instance->cnt = (data >> 24) & 0xFFFF;
    instance->serial = data & 0xFFFFFF;

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->btn);
    }
    subghz_custom_btn_set_max(1);
}

static const char* subghz_protocol_somfy_keytis_get_name_button(uint8_t btn) {
    const char* name_btn[0x10] = {
        "Unknown",
        "0x01",
        "0x02",
        "Prog",
        "Key_1",
        "0x05",
        "0x06",
        "0x07",
        "0x08",
        "0x09",
        "0x0A",
        "0x0B",
        "0x0C",
        "0x0D",
        "0x0E",
        "0x0F"};
    return btn <= 0xf ? name_btn[btn] : name_btn[0];
}

static void somfy_keytis_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderSomfyKeytis* instance = input->decoder;


    subghz_protocol_somfy_keytis_check_remote_controller(&instance->generic);

    subghz_block_generic_global.cnt_is_available = true;
    subghz_block_generic_global.cnt_length_bit = 16;
    subghz_block_generic_global.current_cnt = instance->generic.cnt;

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 4;

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "%lX%08lX%06lX\r\n"
        "Sn:0x%06lX \r\n"
        "Cnt:%04lX\r\n"
        "Btn:%X - %s\r\n",

        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data >> 32),
        (uint32_t)instance->generic.data,
        instance->press_duration_counter,
        instance->generic.serial,
        instance->generic.cnt,
        instance->generic.btn,
        subghz_protocol_somfy_keytis_get_name_button(instance->generic.btn));
}

static const SubGhzDecodeEntry somfy_keytis_items[] = {{.name = "Somfy Keytis", .get_string = somfy_keytis_decode}};
static const SubGhzDecodeRegistry somfy_keytis_registry = {.items = somfy_keytis_items, .size = 1};
static const FlipperAppPluginDescriptor decode_somfy_keytis_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &somfy_keytis_registry};
const FlipperAppPluginDescriptor* decode_somfy_keytis_ep(void) { return &decode_somfy_keytis_descriptor; }
