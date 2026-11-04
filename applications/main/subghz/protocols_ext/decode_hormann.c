/* decode_hormann.fal : SD get_string for hormann (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/hormann.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderHormann {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static void subghz_protocol_hormann_check_remote_controller(SubGhzBlockGeneric* instance);

static void subghz_protocol_hormann_check_remote_controller(SubGhzBlockGeneric* instance) {
    instance->btn = (instance->data >> 8) & 0xF;
}

static void hormann_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderHormann* instance = input->decoder;

    subghz_protocol_hormann_check_remote_controller(&instance->generic);

    subghz_block_generic_global.btn_is_available = false;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 4;

    furi_string_cat_printf(
        output,
        "%s\r\n"
        "%dbit\r\n"
        "Key:0x%03lX%08lX\r\n"
        "Btn:0x%01X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data >> 32),
        (uint32_t)instance->generic.data,
        instance->generic.btn);
}

static const SubGhzDecodeEntry hormann_items[] = {{.name = "Hormann HSM", .get_string = hormann_decode}};
static const SubGhzDecodeRegistry hormann_registry = {.items = hormann_items, .size = 1};
static const FlipperAppPluginDescriptor decode_hormann_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &hormann_registry};
const FlipperAppPluginDescriptor* decode_hormann_ep(void) { return &decode_hormann_descriptor; }
