/* decode_honda_v10.fal : SD get_string for honda_v10 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/blocks/bitbuf.h>
#include <lib/subghz/protocols/honda_v10.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define HV10_HB_CAP      2048

struct SubGhzProtocolDecoderHondaV10 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t hb[SUBGHZ_BITBUF_BYTES(HV10_HB_CAP)];
    uint16_t hb_len;

    uint32_t serial;
    uint8_t btn;
    uint8_t flags;
    uint32_t cnt;
    uint8_t status;
    uint32_t tail;
    bool pol_inv;
};

static const char* honda_v10_btn_name(uint8_t btn);

static const char* honda_v10_btn_name(uint8_t btn) {
    switch(btn) {
    case 0xF: return "Lock";
    case 0xE: return "Unlock";
    default: return "?";
    }
}

static void honda_v10_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderHondaV10* instance = input->decoder;

    furi_string_cat_printf(
        output,
        "%s %dbit (no CRC)\r\n"
        "Key:%016llX\r\n"
        "Serial:%06lX Btn:%X %s\r\n"
        "Cnt:%06lX Typ:%c %s\r\n"
        "Tail(MAC):%08lX\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long long)instance->generic.data,
        (unsigned long)instance->serial,
        instance->btn,
        honda_v10_btn_name(instance->btn),
        (unsigned long)instance->cnt,
        (instance->status & 0x80) ? 'A' : 'B',
        instance->pol_inv ? "INV" : "NOR",
        (unsigned long)instance->tail);
}

static const SubGhzDecodeEntry honda_v10_items[] = {{.name = "Honda V10", .get_string = honda_v10_decode}};
static const SubGhzDecodeRegistry honda_v10_registry = {.items = honda_v10_items, .size = 1};
static const FlipperAppPluginDescriptor decode_honda_v10_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &honda_v10_registry};
const FlipperAppPluginDescriptor* decode_honda_v10_ep(void) { return &decode_honda_v10_descriptor; }
