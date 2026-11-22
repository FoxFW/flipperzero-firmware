
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/belkorn.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderBelKorn {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static void belkorn_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderBelKorn* instance = input->decoder;

    const uint8_t serial_hi = (uint8_t)((instance->generic.data >> 48) & 0xF);
    furi_string_printf(
        output,
        "%s %db\r\n"
        "Key:%lX%08lX\r\n"
        "Key2:%lX%08lX\r\n"
        "Sn:%01X%08lX Btn:%02X\r\n"
        "Cnt:%04lX",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data >> 32),
        (uint32_t)(instance->generic.data & 0xFFFFFFFF),
        (uint32_t)(instance->generic.data_2 >> 32),
        (uint32_t)(instance->generic.data_2 & 0xFFFFFFFF),
        serial_hi,
        instance->generic.serial,
        instance->generic.btn,
        instance->generic.cnt);
}

static const SubGhzDecodeEntry belkorn_items[] = {{.name = "BelKorn", .get_string = belkorn_decode}};
static const SubGhzDecodeRegistry belkorn_registry = {.items = belkorn_items, .size = 1};
static const FlipperAppPluginDescriptor decode_belkorn_descriptor = {
    .appid = SUBGHZ_DECODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION,
    .entry_point = &belkorn_registry};
const FlipperAppPluginDescriptor* decode_belkorn_ep(void) {
    return &decode_belkorn_descriptor;
}
