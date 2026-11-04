/* decode_urmet.fal : SD get_string for urmet (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/urmet.h>
#include <string.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define URMET_MAX_BYTES       40
#define URMET_MAX_GAPS        192
#define URMET_BITBUF          512

typedef enum {
    UrmetStepReset = 0,
    UrmetStepManchester, // collecting short (~100us) Manchester edges
    UrmetStepArmed, // saw Manchester + inter-block gap; wait PPM preamble
    UrmetStepPreambleLow1, // saw H~1te, expect L~1te
    UrmetStepPreambleHigh2, // expect H~1te
    UrmetStepPreambleSync, // expect L~11te sync
    UrmetStepData, // locked: H~1te + variable low gaps
} UrmetDecoderStep;

struct SubGhzProtocolDecoderUrmet {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    UrmetDecoderStep step;
    bool pending_high;
    uint16_t man_edges; // Manchester short-edge count before arming
    uint16_t bit_cnt;
    uint8_t bit_buf[URMET_BITBUF];

    // In-progress collection (may be aborted).
    uint8_t rx_gap_count;
    uint8_t rx_gaps[URMET_MAX_GAPS];

    // Last accepted frame (survives abort / serialize).
    uint8_t gap_count;
    uint8_t gaps[URMET_MAX_GAPS];
    uint8_t raw_bytes[URMET_MAX_BYTES];
    uint8_t byte_count;
};

static void urmet_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderUrmet* instance = input->decoder;


    // Title line stays short (fits left of the freq/modulation drawn at x>=78).
    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit);

    // Full key, 10 bytes (20 hex chars) per line to fit the 128px width.
    for(uint8_t i = 0; i < instance->byte_count; i++) {
        furi_string_cat_printf(output, "%02X", instance->raw_bytes[i]);
        if((i % 10) == 9) furi_string_cat_printf(output, "\r\n");
    }
    if((instance->byte_count % 10) != 0) furi_string_cat_printf(output, "\r\n");
}

static const SubGhzDecodeEntry urmet_items[] = {{.name = "Urmet", .get_string = urmet_decode}};
static const SubGhzDecodeRegistry urmet_registry = {.items = urmet_items, .size = 1};
static const FlipperAppPluginDescriptor decode_urmet_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &urmet_registry};
const FlipperAppPluginDescriptor* decode_urmet_ep(void) { return &decode_urmet_descriptor; }
