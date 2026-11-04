/* decode_continental_rke.fal : SD get_string for continental_rke (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/continental_rke.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define CONTINENTAL_RKE_PAYLOAD_BYTES 14

struct SubGhzProtocolDecoderContinentalRke {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t te_accum; /* accumulated half-bit duration (rtl_433 time_since_last) */
    uint32_t preamble_shift; /* rolling 20-bit window while hunting the preamble */
    bool collecting; /* true once preamble matched, collecting payload bits */
    uint16_t frame_bit; /* payload bits collected so far */
    uint8_t frame[CONTINENTAL_RKE_PAYLOAD_BYTES];
};

static const char*
    subghz_protocol_continental_rke_button_name(uint8_t btn);

static const char*
    subghz_protocol_continental_rke_button_name(uint8_t btn) {
    switch(btn) {
    case 0x1:
        return "Lock";
    case 0x3:
        return "Unlock";
    case 0x9:
        return "Trunk(hold)";
    case 0xA:
        return "Trunk/Panic";
    case 0xB:
        return "Panic(hold)";
    default:
        return "?";
    }
}

static void continental_rke_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderContinentalRke* instance = input->decoder;


    /* Re-derive ID + button from the stored 64-bit Key so this is stable across
     * save/load (bytes 0..3 = ID, byte 5 high-nibble = button). */
    uint64_t data = instance->generic.data;
    uint32_t id = (uint32_t)(data >> 32);
    uint8_t button = (uint8_t)((data >> 20) & 0x0F);

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%016llX\r\n"
        "ID:%08lX Btn:%X %s\r\n"
        "Cnt:%06lX Enc:%08lX",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long long)data,
        (unsigned long)id,
        button,
        subghz_protocol_continental_rke_button_name(button),
        (unsigned long)instance->generic.cnt,
        (unsigned long)instance->generic.seed);
}

static const SubGhzDecodeEntry continental_rke_items[] = {{.name = "Continental RKE", .get_string = continental_rke_decode}};
static const SubGhzDecodeRegistry continental_rke_registry = {.items = continental_rke_items, .size = 1};
static const FlipperAppPluginDescriptor decode_continental_rke_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &continental_rke_registry};
const FlipperAppPluginDescriptor* decode_continental_rke_ep(void) { return &decode_continental_rke_descriptor; }
