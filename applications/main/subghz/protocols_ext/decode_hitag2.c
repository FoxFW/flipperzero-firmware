/* decode_hitag2.fal : SD get_string for hitag2 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/blocks/bitbuf.h>
#include <lib/subghz/protocols/hitag2.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <furi.h>
#include <furi_hal.h>

#define HITAG2_ALL_BITS_MAX  400
#define HITAG2_MAXDUR        700

struct SubGhzProtocolDecoderHitag2 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    // Raw signed pulse durations of the current burst (+ = high, - = low).
    // te_short is estimated per burst, so a single decoder handles the whole
    // Hitag2 bitrate range (te≈90µs Fiat/Alfa .. te≈240µs Ford Ka).
    int16_t durs[HITAG2_MAXDUR];
    uint16_t dur_count;

    // Scratch demodulated bits (filled per polarity in process()).
    uint8_t all_bits[SUBGHZ_BITBUF_BYTES(HITAG2_ALL_BITS_MAX)];

    uint32_t uid;   // 32-bit identifier
    uint8_t  btn;   // 4-bit button id
    uint16_t lctr;  // 10-bit cleartext counter (low bits)
    uint32_t ks;    // 32-bit keystream
    uint8_t  chk;   // 8-bit parity byte
};

static const char* hitag2_btn_name(uint8_t btn);

static const char* hitag2_btn_name(uint8_t btn) {
    // Ford Ka mapping; other manufacturers may differ.
    switch(btn) {
    case 0x3: return "Lock";
    case 0x5: return "Unlock";
    case 0x9: return "Trunk";
    default: return "?";
    }
}

static void hitag2_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderHitag2* instance = input->decoder;


    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%016llX\r\n"
        "Sn:%08lX Btn:%X [%s]\r\n"
        "Cnt:%03X Hop:%08lX Chk:%02X",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long long)instance->generic.data,
        (unsigned long)instance->uid,
        instance->btn,
        hitag2_btn_name(instance->btn),
        instance->lctr,
        (unsigned long)instance->ks,
        instance->chk);
}

static const SubGhzDecodeEntry hitag2_items[] = {{.name = "Hitag2", .get_string = hitag2_decode}};
static const SubGhzDecodeRegistry hitag2_registry = {.items = hitag2_items, .size = 1};
static const FlipperAppPluginDescriptor decode_hitag2_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &hitag2_registry};
const FlipperAppPluginDescriptor* decode_hitag2_ep(void) { return &decode_hitag2_descriptor; }
