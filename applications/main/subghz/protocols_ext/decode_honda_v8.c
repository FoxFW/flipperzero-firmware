/* decode_honda_v8.fal : SD get_string for honda_v8 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/honda_v8.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderHondaV8 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t parser_step;
    uint64_t data;
    uint16_t count;
    bool have_last;
    uint16_t last_rc;

    uint32_t serial;
    uint8_t btn;
    uint16_t cnt;
    uint8_t key_idx;
    uint32_t enc;
    bool crc_ok;
};

static const char* honda_v8_btn_name(uint8_t btn);

static const char* honda_v8_btn_name(uint8_t btn) {
    switch(btn) {
    case 0x0: return "Panic";
    case 0x1: return "Lock";
    case 0x2: return "Trunk";
    case 0x4: return "Remote Start";
    case 0x8: return "Unlock";
    case 0x9: return "Unlock x2";
    default: return "?";
    }
}

static void honda_v8_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderHondaV8* instance = input->decoder;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%016llX\r\n"
        "Serial:%07lX Btn:%X %s\r\n"
        "Cnt:%04X Enc:%08lX\r\n"
        "KeeLoq key[%u] %s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long long)instance->generic.data,
        (unsigned long)instance->serial,
        instance->btn,
        honda_v8_btn_name(instance->btn),
        instance->cnt,
        (unsigned long)instance->enc,
        instance->key_idx,
        instance->crc_ok ? "OK" : "--");
}

static const SubGhzDecodeEntry honda_v8_items[] = {{.name = "Honda V8", .get_string = honda_v8_decode}};
static const SubGhzDecodeRegistry honda_v8_registry = {.items = honda_v8_items, .size = 1};
static const FlipperAppPluginDescriptor decode_honda_v8_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &honda_v8_registry};
const FlipperAppPluginDescriptor* decode_honda_v8_ep(void) { return &decode_honda_v8_descriptor; }
