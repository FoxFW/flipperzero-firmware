/* decode_ford_v0.fal : SD get_string for ford_v0 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/ford_v0.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/toolbox/manchester_decoder.h>

typedef struct SubGhzProtocolDecoderFordV0 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    
    ManchesterState manchester_state;
    
    uint64_t data_low;
    uint64_t data_high;
    uint8_t bit_count;
    
    uint16_t header_count;
    
    uint64_t key1;
    uint16_t key2;
    uint32_t serial;
    uint8_t button;
    uint32_t count;
    bool crc_valid;
} SubGhzProtocolDecoderFordV0;

static const char* ford_v0_get_button_name(uint8_t btn);

static const char* ford_v0_get_button_name(uint8_t btn) {
    switch(btn) {
        case 0x01: return "Lock";
        case 0x02: return "Unlock";
        case 0x04: return "Trunk";
        default: return "??";
    }
}

static void ford_v0_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderFordV0* instance = input->decoder;

    
    uint32_t code_found_hi = (uint32_t)(instance->key1 >> 32);
    uint32_t code_found_lo = (uint32_t)(instance->key1 & 0xFFFFFFFF);
    
    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key1:%08lX%08lX\r\n"
        "Key2:%04X Sn:%08lX\r\n"
        "Btn:%02X:[%s] Cnt:%06lX\r\n"
        "CheckSum:%02X CRC:%02X %s",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        code_found_hi,
        code_found_lo,
        instance->key2,
        instance->serial,
        instance->button,
        ford_v0_get_button_name(instance->button),
        instance->count,
        (instance->key2 >> 8) & 0xFF,
        instance->key2 & 0xFF,
        instance->crc_valid ? "(OK)" : "(FAIL)"
    );
}

static const SubGhzDecodeEntry ford_v0_items[] = {{.name = "FORD V0", .get_string = ford_v0_decode}};
static const SubGhzDecodeRegistry ford_v0_registry = {.items = ford_v0_items, .size = 1};
static const FlipperAppPluginDescriptor decode_ford_v0_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &ford_v0_registry};
const FlipperAppPluginDescriptor* decode_ford_v0_ep(void) { return &decode_ford_v0_descriptor; }
