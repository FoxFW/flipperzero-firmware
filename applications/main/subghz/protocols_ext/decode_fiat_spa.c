/* decode_fiat_spa.fal : SD get_string for fiat_spa (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/fiat_spa.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockGeneric generic;
} SubGhzProtocolCommonFiatSpa;

static void fiat_spa_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolCommonFiatSpa* instance = input->decoder;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Fix:%08lX\r\n"
        "Hop:%08lX\r\n"
        "EndByte:%02X",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data >> 32),
        (uint32_t)(instance->generic.data & 0xFFFFFFFF),
        instance->generic.serial,
        instance->generic.cnt,
        instance->generic.btn);
}

static const SubGhzDecodeEntry fiat_spa_items[] = {{.name = "FIAT SPA", .get_string = fiat_spa_decode}};
static const SubGhzDecodeRegistry fiat_spa_registry = {.items = fiat_spa_items, .size = 1};
static const FlipperAppPluginDescriptor decode_fiat_spa_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &fiat_spa_registry};
const FlipperAppPluginDescriptor* decode_fiat_spa_ep(void) { return &decode_fiat_spa_descriptor; }
