/* decode_gm_rke.fal : SD get_string for gm_rke (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/gm_rke.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define GM_RKE_MAX_BITS  256

struct SubGhzProtocolDecoderGmRke {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t bit_count;
    uint8_t bits[GM_RKE_MAX_BITS]; /* one 0/1 per PPM symbol, MSB-first order */

    /* Live-only extras (not serialized; recovered on decode, 0 after load). */
    uint8_t unknown_prefix; /* byte[1] */
    uint32_t encrypted;     /* byte[10..12], 24-bit */
};

static void subghz_protocol_gm_rke_check_remote_controller(SubGhzBlockGeneric* generic);
static const char* subghz_protocol_gm_rke_button_name(uint8_t btn);

static void subghz_protocol_gm_rke_check_remote_controller(SubGhzBlockGeneric* generic) {
    generic->btn = (uint8_t)(generic->data & 0x7);
    generic->cnt = (uint32_t)((generic->data >> 3) & 0xFFFFFF);
    generic->serial = (uint32_t)((generic->data >> 27) & 0xFFFFFFFF);
}

static const char* subghz_protocol_gm_rke_button_name(uint8_t btn) {
    switch(btn) {
    case 0x1:
        return "Unlock";
    case 0x2:
        return "Lock";
    case 0x3:
        return "Trunk";
    case 0x4:
        return "Panic";
    default:
        return "?";
    }
}

static void gm_rke_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderGmRke* instance = input->decoder;


    subghz_protocol_gm_rke_check_remote_controller(&instance->generic);

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "ID:%02X%08lX\r\n"
        "Btn:%X (%s)\r\n"
        "Seq:%06lX  Enc:%06lX",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        instance->unknown_prefix,
        (unsigned long)instance->generic.serial,
        instance->generic.btn,
        subghz_protocol_gm_rke_button_name(instance->generic.btn),
        (unsigned long)instance->generic.cnt,
        (unsigned long)instance->encrypted);
}

static const SubGhzDecodeEntry gm_rke_items[] = {{.name = "GM ABO1502T", .get_string = gm_rke_decode}};
static const SubGhzDecodeRegistry gm_rke_registry = {.items = gm_rke_items, .size = 1};
static const FlipperAppPluginDescriptor decode_gm_rke_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &gm_rke_registry};
const FlipperAppPluginDescriptor* decode_gm_rke_ep(void) { return &decode_gm_rke_descriptor; }
