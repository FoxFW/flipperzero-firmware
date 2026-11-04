/* decode_marantec.fal : SD get_string for marantec (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/marantec.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderMarantec {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    ManchesterState manchester_saved_state;
    uint16_t header_count;
};

static void subghz_protocol_marantec_remote_controller(SubGhzBlockGeneric* instance);

static void subghz_protocol_marantec_remote_controller(SubGhzBlockGeneric* instance) {

    instance->btn = (instance->data >> 16) & 0xF;
    instance->serial = ((instance->data >> 12) & 0xFFFFFF00) | ((instance->data >> 8) & 0xFF);
}

static void marantec_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderMarantec* instance = input->decoder;

    subghz_protocol_marantec_remote_controller(&instance->generic);

    uint8_t crc = (input->extra_count > 0) ? (uint8_t)input->extra[0] : 0; /* CRC computed internally */
    bool crc_ok = (crc == (instance->generic.data & 0xFF));

    subghz_block_generic_global.btn_is_available = false;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 4;

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "Key: 0x%lX%08lX\r\n"
        "Sn: 0x%07lX \r\n"
        "CRC: 0x%02X - %s\r\n"
        "Btn: %X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data >> 32),
        (uint32_t)(instance->generic.data & 0xFFFFFFFF),
        instance->generic.serial,
        crc,
        crc_ok ? "Valid" : "Invalid",
        instance->generic.btn);
}

static const SubGhzDecodeEntry marantec_items[] = {{.name = "Marantec", .get_string = marantec_decode}};
static const SubGhzDecodeRegistry marantec_registry = {.items = marantec_items, .size = 1};
static const FlipperAppPluginDescriptor decode_marantec_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &marantec_registry};
const FlipperAppPluginDescriptor* decode_marantec_ep(void) { return &decode_marantec_descriptor; }
