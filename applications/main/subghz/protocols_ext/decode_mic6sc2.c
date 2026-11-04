/* decode_mic6sc2.fal : SD get_string for mic6sc2 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/mic6sc2.h>
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
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define MIC6SC2_MAX_BYTES 11U /* 88 bits */

typedef struct SubGhzProtocolDecoderMic6sc2 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;

    uint8_t frame[MIC6SC2_MAX_BYTES];
    uint16_t bit_count;

    uint32_t encrypted;
    uint16_t sequence;
    uint8_t button;
} SubGhzProtocolDecoderMic6sc2;

static void mic6sc2_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderMic6sc2* instance = input->decoder;


    const char* button_name;
    switch(instance->button) {
    case 0x1:
        button_name = "Unlock";
        break;
    case 0x2:
        button_name = "Lock";
        break;
    case 0x3:
        button_name = "Trunk";
        break;
    case 0x4:
        button_name = "Panic";
        break;
    default:
        button_name = "?";
        break;
    }

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key: %016llX\r\n"
        "Enc: %08lX\r\n"
        "Btn: %01X - %s\r\n"
        "Seq: %04X  CS:OK\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long long)instance->generic.data,
        (unsigned long)instance->encrypted,
        instance->button,
        button_name,
        instance->sequence);
}

static const SubGhzDecodeEntry mic6sc2_items[] = {{.name = "Microchip 6SC2", .get_string = mic6sc2_decode}};
static const SubGhzDecodeRegistry mic6sc2_registry = {.items = mic6sc2_items, .size = 1};
static const FlipperAppPluginDescriptor decode_mic6sc2_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &mic6sc2_registry};
const FlipperAppPluginDescriptor* decode_mic6sc2_ep(void) { return &decode_mic6sc2_descriptor; }
