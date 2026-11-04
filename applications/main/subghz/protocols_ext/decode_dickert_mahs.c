/* decode_dickert_mahs.fal : SD get_string for dickert_mahs (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/dickert_mahs.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_rtc.h>

struct SubGhzProtocolDecoderDickertMAHS {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t tmp[2];
    uint8_t tmp_cnt;
};

static void subghz_protocol_encoder_dickert_mahs_parse_buffer(
    SubGhzProtocolDecoderDickertMAHS* instance,
    FuriString* output);

static void subghz_protocol_encoder_dickert_mahs_parse_buffer(
    SubGhzProtocolDecoderDickertMAHS* instance,
    FuriString* output) {

    uint64_t data = instance->generic.data;
    uint8_t bits[36] = {};

    for(int i = 35; i >= 0; i--) {
        if(data & 1) {
            bits[i] = 1;
        }
        data >>= 1;
    }

    FuriString* code = furi_string_alloc();
    for(size_t i = 0; i < 35; i += 2) {
        uint8_t dip = (bits[i] << 1) + bits[i + 1];

        if(dip == 0x01) {
            furi_string_cat(code, "0");
        } else if(dip == 0x00) {
            furi_string_cat(code, "-");
        } else if(dip == 0x03) {
            furi_string_cat(code, "+");
        } else {
            furi_string_cat(code, "?");
        }
    }

    FuriString* user_dips = furi_string_alloc();
    FuriString* fact_dips = furi_string_alloc();
    furi_string_set_n(user_dips, code, 0, 10);
    furi_string_set_n(fact_dips, code, 10, 8);

    furi_string_cat_printf(
        output,
        "%s\r\n"
        "User-Dips:\t%s\r\n"
        "Fac-Code:\t%s\r\n",
        instance->generic.protocol_name,
        furi_string_get_cstr(user_dips),
        furi_string_get_cstr(fact_dips));
    furi_string_free(user_dips);
    furi_string_free(fact_dips);
    furi_string_free(code);
}

static void dickert_mahs_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderDickertMAHS* instance = input->decoder;

    subghz_protocol_encoder_dickert_mahs_parse_buffer(instance, output);
}

static const SubGhzDecodeEntry dickert_mahs_items[] = {{.name = "Dickert_MAHS", .get_string = dickert_mahs_decode}};
static const SubGhzDecodeRegistry dickert_mahs_registry = {.items = dickert_mahs_items, .size = 1};
static const FlipperAppPluginDescriptor decode_dickert_mahs_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &dickert_mahs_registry};
const FlipperAppPluginDescriptor* decode_dickert_mahs_ep(void) { return &decode_dickert_mahs_descriptor; }
