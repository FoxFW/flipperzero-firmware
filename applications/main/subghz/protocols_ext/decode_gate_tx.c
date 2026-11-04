/* decode_gate_tx.fal : SD get_string for gate_tx (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/gate_tx.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderGateTx {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static void subghz_protocol_gate_tx_check_remote_controller(SubGhzBlockGeneric* instance);

static void subghz_protocol_gate_tx_check_remote_controller(SubGhzBlockGeneric* instance) {
    uint32_t code_found_reverse =
        subghz_protocol_blocks_reverse_key(instance->data, instance->data_count_bit);

    instance->serial = (code_found_reverse & 0xFF) << 12 |
                       ((code_found_reverse >> 8) & 0xFF) << 4 |
                       ((code_found_reverse >> 20) & 0x0F);
    instance->btn = ((code_found_reverse >> 16) & 0x0F);
}

static void gate_tx_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderGateTx* instance = input->decoder;

    subghz_protocol_gate_tx_check_remote_controller(&instance->generic);

    subghz_block_generic_global.btn_is_available = false;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 4;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%06lX\r\n"
        "Sn:%05lX  Btn:%X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data & 0xFFFFFF),
        instance->generic.serial,
        instance->generic.btn);
}

static const SubGhzDecodeEntry gate_tx_items[] = {{.name = "GateTX", .get_string = gate_tx_decode}};
static const SubGhzDecodeRegistry gate_tx_registry = {.items = gate_tx_items, .size = 1};
static const FlipperAppPluginDescriptor decode_gate_tx_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &gate_tx_registry};
const FlipperAppPluginDescriptor* decode_gate_tx_ep(void) { return &decode_gate_tx_descriptor; }
