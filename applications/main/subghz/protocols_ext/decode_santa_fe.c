/* decode_santa_fe.fal : SD get_string for santa_fe (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/santa_fe.h>
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

#define SF_BYTES       10

struct SubGhzProtocolDecoderSantaFe {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t data[SF_BYTES];
    uint16_t bit_count;
    uint32_t rolling;
};

static const char* sf_button_name(uint8_t b);

static const char* sf_button_name(uint8_t b) {
    switch(b) {
    case 0x01:
        return "Lock";
    case 0x02:
        return "Unlock";
    case 0x04:
        return "Trunk";
    case 0x08:
        return "Panic";
    default:
        return "Unknown";
    }
}

static void santa_fe_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderSantaFe* instance = input->decoder;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Roll:%08lX\r\n"
        "Sn:%06lX Cnt:%02X\r\n"
        "Btn:%s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)instance->rolling,
        (unsigned long)(instance->generic.serial & 0xFFFFFF),
        (unsigned int)(instance->generic.cnt & 0xFF),
        sf_button_name(instance->generic.btn));
}

static const SubGhzDecodeEntry santa_fe_items[] = {{.name = "SantaFe 13-16", .get_string = santa_fe_decode}};
static const SubGhzDecodeRegistry santa_fe_registry = {.items = santa_fe_items, .size = 1};
static const FlipperAppPluginDescriptor decode_santa_fe_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &santa_fe_registry};
const FlipperAppPluginDescriptor* decode_santa_fe_ep(void) { return &decode_santa_fe_descriptor; }
