/* decode_opel_v1.fal : SD get_string for opel_v1 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/opel_v1.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>
#include <string.h>

#define OPEL_V1_BTN_TRUNK      0x20U
#define OPEL_V1_BTN_LOCK       0x04U
#define OPEL_V1_BTN_UNLOCK     0x08U

struct SubGhzProtocolDecoderOpelV1 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;
    uint64_t data_high;
    uint64_t data_low;
    uint16_t bit_count;

    uint8_t xtea_key[16];
    uint64_t cipher;
    uint32_t serial;
    uint8_t button;
    uint8_t burst;
    uint8_t tail;
    uint8_t check_byte;
    uint32_t count;
    bool valid;
};

static const char* opel_v1_button_name(uint8_t button);

static const char* opel_v1_button_name(uint8_t button) {
    switch(button) {
    case OPEL_V1_BTN_TRUNK:
        return "Trunk";
    case OPEL_V1_BTN_LOCK:
        return "Lock";
    case OPEL_V1_BTN_UNLOCK:
        return "Unlock";
    default:
        return "Unknown";
    }
}

static void opel_v1_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderOpelV1* instance = input->decoder;


    furi_string_printf(
        output,
        "%s %dbit\r\n"
        "Sn:%06lX  Btn:%02X - %s\r\n"
        "Cipher:%016llX\r\n"
        "Cnt:%08lX [%s]\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)instance->serial,
        instance->button,
        opel_v1_button_name(instance->button),
        (unsigned long long)instance->cipher,
        (unsigned long)instance->count,
        instance->valid ? "OK" : "BAD");
}

static const SubGhzDecodeEntry opel_v1_items[] = {{.name = "Opel V1", .get_string = opel_v1_decode}};
static const SubGhzDecodeRegistry opel_v1_registry = {.items = opel_v1_items, .size = 1};
static const FlipperAppPluginDescriptor decode_opel_v1_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &opel_v1_registry};
const FlipperAppPluginDescriptor* decode_opel_v1_ep(void) { return &decode_opel_v1_descriptor; }
