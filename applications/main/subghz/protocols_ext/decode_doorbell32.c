/* decode_doorbell32.fal : SD get_string for doorbell32 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/doorbell32.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define DOORBELL32_GUARD_TIME 14u

struct SubGhzProtocolDecoderDoorbell32 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t te;
    uint32_t last_data;
};

static void doorbell32_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderDoorbell32* instance = input->decoder;


    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%08lX\r\n"
        "Te:%luus  GT:Te*%u",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data & 0xFFFFFFFF),
        instance->te,
        DOORBELL32_GUARD_TIME);
}

static const SubGhzDecodeEntry doorbell32_items[] = {{.name = "Doorbell32", .get_string = doorbell32_decode}};
static const SubGhzDecodeRegistry doorbell32_registry = {.items = doorbell32_items, .size = 1};
static const FlipperAppPluginDescriptor decode_doorbell32_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &doorbell32_registry};
const FlipperAppPluginDescriptor* decode_doorbell32_ep(void) { return &decode_doorbell32_descriptor; }
