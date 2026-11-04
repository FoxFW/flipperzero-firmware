/* decode_hyundai_kia_rio.fal : SD get_string for hyundai_kia_rio (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/hyundai_kia_rio.h>
#include <furi.h>
#include <string.h>
#include <lib/subghz/types.h>
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>
#include <flipper_format/flipper_format.h>
#include <lib/subghz/protocols/pp_auto_common.h>

struct SubGhzProtocolDecoderHyundaiKiaRio {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint64_t data;
    uint8_t bit_count;
};

static const char* hkr_button_name(uint16_t mask);

static const char* hkr_button_name(uint16_t mask) {
    switch(mask) {
    case 0x0100:
        return "Lock";
    case 0x0200:
        return "Unlock";
    case 0x0400:
        return "Trunk";
    case 0x0800:
        return "Panic";
    default:
        return "Unknown";
    }
}

static void hyundai_kia_rio_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderHyundaiKiaRio* instance = input->decoder;

    uint16_t mask = (uint16_t)((instance->generic.data >> 16) & 0xFFFF);

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Sn:%08lX Btn:%s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)(instance->generic.data >> 32),
        (unsigned long)(instance->generic.data & 0xFFFFFFFF),
        (unsigned long)instance->generic.serial,
        hkr_button_name(mask));
}

static const SubGhzDecodeEntry hyundai_kia_rio_items[] = {{.name = "Hyundai/Kia RIO", .get_string = hyundai_kia_rio_decode}};
static const SubGhzDecodeRegistry hyundai_kia_rio_registry = {.items = hyundai_kia_rio_items, .size = 1};
static const FlipperAppPluginDescriptor decode_hyundai_kia_rio_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &hyundai_kia_rio_registry};
const FlipperAppPluginDescriptor* decode_hyundai_kia_rio_ep(void) { return &decode_hyundai_kia_rio_descriptor; }
