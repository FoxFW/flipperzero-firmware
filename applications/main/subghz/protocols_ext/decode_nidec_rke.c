/* decode_nidec_rke.fal : SD get_string for nidec_rke (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/nidec_rke.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define NIDEC_HEAD_BITS    48 /* sequence+id+byte5 always fully captured */

struct SubGhzProtocolDecoderNidecRke {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t sync_reg; /* rolling 32-bit window of raw (non-inverted) bits */
    uint64_t payload; /* inverted payload bits, MSB-first */
    uint16_t payload_bits; /* count collected so far */
};

static inline uint8_t nidec_byte(uint64_t data, uint8_t index);

static inline uint8_t nidec_byte(uint64_t data, uint8_t index) {
    /* byte 0 = most significant, matching rtl_433 bytes[0..7] */
    return (uint8_t)((data >> (56 - 8 * index)) & 0xFF);
}

static void nidec_rke_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderNidecRke* instance = input->decoder;


    uint64_t data = instance->generic.data;
    uint16_t bits = instance->generic.data_count_bit;

    uint32_t sequence = ((uint32_t)nidec_byte(data, 0) << 8) | nidec_byte(data, 1);
    uint32_t id = ((uint32_t)nidec_byte(data, 2) << 16) | ((uint32_t)nidec_byte(data, 3) << 8) |
                  nidec_byte(data, 4);
    uint8_t button = nidec_byte(data, 5) & 0x0F;
    uint32_t security = ((uint32_t)nidec_byte(data, 6) << 8) | nidec_byte(data, 7);

    int security_bits = (int)bits - NIDEC_HEAD_BITS;
    if(security_bits < 0) security_bits = 0;
    if(security_bits > 16) security_bits = 16;

    const char* button_str;
    switch(button) {
    case 0x3:
        button_str = "Lock";
        break;
    case 0x4:
        button_str = "Unlock";
        break;
    case 0x5:
        button_str = "Trunk/Panic";
        break;
    case 0x6:
        button_str = "Panic Long";
        break;
    case 0xF:
        button_str = "Trunk Long";
        break;
    default:
        button_str = "?";
        break;
    }

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%016llX\r\n"
        "ID:%06lX  Seq:%04lX\r\n"
        "Btn:%X %s\r\n"
        "Sec:%04lX (%d bit)",
        instance->generic.protocol_name,
        bits,
        data,
        id,
        sequence,
        button,
        button_str,
        security,
        security_bits);
}

static const SubGhzDecodeEntry nidec_rke_items[] = {{.name = "Nidec RKE", .get_string = nidec_rke_decode}};
static const SubGhzDecodeRegistry nidec_rke_registry = {.items = nidec_rke_items, .size = 1};
static const FlipperAppPluginDescriptor decode_nidec_rke_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &nidec_rke_registry};
const FlipperAppPluginDescriptor* decode_nidec_rke_ep(void) { return &decode_nidec_rke_descriptor; }
