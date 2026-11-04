/* decode_honda_v9.fal : SD get_string for honda_v9 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/blocks/bitbuf.h>
#include <lib/subghz/protocols/honda_v9.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define HV9_HB_CAP     512

struct SubGhzProtocolDecoderHondaV9 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t hb[SUBGHZ_BITBUF_BYTES(HV9_HB_CAP)];
    uint16_t hb_len;

    uint32_t serial;
    uint8_t btn;
    uint16_t cnt;
    uint8_t hdr;
    uint8_t flags;
    uint32_t fingerprint;
    uint8_t crc;
    bool crc_ok;
    bool pol_inv;
    uint64_t tail; // decoded[8..13] (48 bits)
};

static const char* honda_v9_btn_name(uint8_t btn);

static const char* honda_v9_btn_name(uint8_t btn) {
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

static void honda_v9_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderHondaV9* instance = input->decoder;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%016llX\r\n"
        "Serial:%05lX Btn:%X %s\r\n"
        "Cnt:%04X FP:%08lX\r\n"
        "CRC:%02X %s %s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long long)instance->generic.data,
        (unsigned long)instance->serial,
        instance->btn,
        honda_v9_btn_name(instance->btn),
        instance->cnt,
        (unsigned long)instance->fingerprint,
        instance->crc,
        instance->crc_ok ? "OK" : "~1",
        instance->pol_inv ? "INV" : "NOR");
}

static const SubGhzDecodeEntry honda_v9_items[] = {{.name = "Honda V9", .get_string = honda_v9_decode}};
static const SubGhzDecodeRegistry honda_v9_registry = {.items = honda_v9_items, .size = 1};
static const FlipperAppPluginDescriptor decode_honda_v9_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &honda_v9_registry};
const FlipperAppPluginDescriptor* decode_honda_v9_ep(void) { return &decode_honda_v9_descriptor; }
