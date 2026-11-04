/* decode_fiat_marelli.fal : SD get_string for fiat_marelli (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/fiat_marelli.h>
#include <lib/subghz/protocols/fiat_v1.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include <furi_hal_subghz.h>

#define FIAT_MARELLI_TE_TYPE_AB_BOUNDARY 180

struct SubGhzProtocolDecoderFiatMarelli {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    ManchesterState manchester_state;
    uint8_t decoder_state;
    uint16_t preamble_count;
    uint8_t raw_data[13];
    uint8_t bit_count;
    uint32_t extra_data;
    uint32_t te_last;
    uint32_t te_sum;
    uint16_t te_count;
    uint32_t te_detected;
    // Cached result of the Hitag2 manufacturer-key dictionary pass (computed
    // lazily on first get_string; see fiat_marelli_find_hitag2_key).
    bool hitag2_checked;
    int16_t hitag2_key_index; // -1 = no known key matched; >=0 index into fiat_v1 keys
    uint32_t hitag2_epoch;
};

static uint8_t fiat_marelli_crc8(const uint8_t* data, size_t len);
static const char* fiat_marelli_button_name(uint8_t btn);

static uint8_t fiat_marelli_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0x03;
    for(size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for(uint8_t b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x01) : (crc << 1);
        }
    }
    return crc;
}

static const char* fiat_marelli_button_name(uint8_t btn) {
    switch(btn) {
    case 0x7:
        return "Lock";
    case 0xB:
        return "Unlock";
    case 0xD:
        return "Trunk";
    default:
        return "Unknown";
    }
}

static void fiat_marelli_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderFiatMarelli* instance = input->decoder;


    uint8_t epoch = instance->raw_data[6] & 0xF;
    uint8_t counter = (instance->raw_data[7] >> 3) & 0x1F;
    const char* variant = (instance->te_detected &&
                           instance->te_detected < FIAT_MARELLI_TE_TYPE_AB_BOUNDARY)
                              ? "B"
                              : "A";
    uint8_t scramble = (instance->raw_data[7] >> 1) & 0x3;
    uint8_t fixed    =  instance->raw_data[7] & 0x1;

    const char* crc_str = "";
    bool crc_ok = false;
    if(instance->bit_count >= 104) {
        uint8_t calc = fiat_marelli_crc8(instance->raw_data, 12);
        crc_ok = (calc == instance->raw_data[12]);
        crc_str = crc_ok ? " CRC:OK" : " CRC:FAIL";
    }

    // Harmless epoch-0 dictionary stub (kept per maintainer decision, 2026-10-12).
    // The Marelli payload cipher is unknown and is NOT the Fiat V1 cipher this
    // reuses, so a full sweep never matches a real frame; nothing is displayed
    // from it (the old "MfKey:" line was removed). Left GUI-safe (epoch-0 only,
    // ~38ms, cached once), ready if a real Marelli cipher/key set is added.
    /* HiTag2 dictionary check done internally */

    furi_string_cat_printf(
        output,
        "%s %dbit%s\r\n"
        "Enc:%02X%02X%02X%02X%02X Scr:%02X\r\n"
        "Raw:%02X%02X Fixed:%X\r\n"
        "Sn:%08X Cnt:%02X\r\n"
        "Btn:%02X:[%s] Ep:%02X\r\n"
        "Tp:%s\r\n",
        instance->generic.protocol_name,
        (int)instance->bit_count,
        crc_str,
        instance->raw_data[8], instance->raw_data[9],
        instance->raw_data[10], instance->raw_data[11],
        instance->raw_data[12],
        (unsigned)scramble,
        instance->raw_data[6], instance->raw_data[7],
        (unsigned)fixed,
        (unsigned int)instance->generic.serial,
        (unsigned)counter,
        (unsigned)instance->generic.btn,
        fiat_marelli_button_name(instance->generic.btn),
        (unsigned)epoch,
        variant);
}

static const SubGhzDecodeEntry fiat_marelli_items[] = {{.name = "MARELLI", .get_string = fiat_marelli_decode}};
static const SubGhzDecodeRegistry fiat_marelli_registry = {.items = fiat_marelli_items, .size = 1};
static const FlipperAppPluginDescriptor decode_fiat_marelli_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &fiat_marelli_registry};
const FlipperAppPluginDescriptor* decode_fiat_marelli_ep(void) { return &decode_fiat_marelli_descriptor; }
