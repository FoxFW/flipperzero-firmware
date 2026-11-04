/* decode_siemens_vdo.fal : SD get_string for siemens_vdo (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/siemens_vdo.h>
#include <lib/subghz/protocols/pp_auto_common.h>
#include <lib/toolbox/manchester_decoder.h>

#define SIEMENS_VDO_PAYLOAD_BYTES 12U

struct SubGhzProtocolDecoderSiemensVdo {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;
    uint16_t sync_shift; // sliding 16-bit window while hunting for the sync word
    uint16_t sync_bits; // number of bits shifted in (saturates at 16)
    bool synced; // sync word found, now collecting payload
    bool invert; // sync matched inverted -> invert captured bits
    uint16_t data_bit_count; // payload bits collected so far
    uint8_t bytes[SIEMENS_VDO_PAYLOAD_BYTES];
    uint8_t crc; // captured check byte
    bool crc_check; // XOR integrity result
};

static void siemens_vdo_check_remote_controller(SubGhzProtocolDecoderSiemensVdo* instance);
static const char* siemens_vdo_get_button_name(uint8_t btn);

static void siemens_vdo_check_remote_controller(SubGhzProtocolDecoderSiemensVdo* instance) {
    uint64_t data = instance->generic.data;
    instance->generic.btn = (uint8_t)((data >> 56) & 0xFF);
    instance->generic.cnt = (uint32_t)((data >> 40) & 0xFFFF);
    instance->crc = (uint8_t)(data & 0xFF);
}

static const char* siemens_vdo_get_button_name(uint8_t btn) {
    switch(btn) {
    case 0x01:
        return "Lock";
    case 0x02:
        return "Unlock";
    case 0x04:
        return "Trunk";
    case 0x0F:
        return "Panic";
    case 0x10:
        return "Left Door";
    case 0x20:
        return "Right Door";
    default:
        return "??";
    }
}

static void siemens_vdo_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderSiemensVdo* instance = input->decoder;


    siemens_vdo_check_remote_controller(instance);

    uint8_t button = instance->generic.btn;
    uint16_t sequence = (uint16_t)instance->generic.cnt;
    uint32_t encrypted = (uint32_t)((instance->generic.data >> 8) & 0xFFFFFFFF);

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Id:%08lX\r\n"
        "Btn:%02X:%s\r\n"
        "Seq:%04X\r\n"
        "Enc:%08lX  MIC:%02X %s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)instance->generic.serial,
        button,
        siemens_vdo_get_button_name(button),
        sequence,
        (unsigned long)encrypted,
        instance->crc,
        instance->crc_check ? "OK" : "WRONG");
}

static const SubGhzDecodeEntry siemens_vdo_items[] = {{.name = "Siemens VDO", .get_string = siemens_vdo_decode}};
static const SubGhzDecodeRegistry siemens_vdo_registry = {.items = siemens_vdo_items, .size = 1};
static const FlipperAppPluginDescriptor decode_siemens_vdo_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &siemens_vdo_registry};
const FlipperAppPluginDescriptor* decode_siemens_vdo_ep(void) { return &decode_siemens_vdo_descriptor; }
