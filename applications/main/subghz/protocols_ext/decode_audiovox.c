/* decode_audiovox.fal : SD get_string for audiovox (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/audiovox.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define AUDIOVOX_FRAME_BITS 25

struct SubGhzProtocolDecoderAudiovox {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

typedef struct SubGhzProtocolDecoderAudiovox SubGhzProtocolDecoderAudiovox;

typedef struct {
    uint16_t id;
    uint8_t btn_bits; /* inverted byte2 masked to the four button positions */
} AudiovoxFrame;

static bool subghz_protocol_audiovox_parse(uint64_t data, AudiovoxFrame* out);

static bool subghz_protocol_audiovox_parse(uint64_t data, AudiovoxFrame* out) {
    /* MSB-first collection: first received bit sits at bit24. */
    uint8_t b0 = (uint8_t)((data >> 17) & 0xFF); /* bits  0..7  */
    uint8_t b1 = (uint8_t)((data >> 9) & 0xFF); /* bits  8..15 */
    uint8_t b2 = (uint8_t)((data >> 1) & 0xFF); /* bits 16..23 */

    /* (1) sanity on the RAW byte2 */
    if((b2 & 0xAA) || (b2 == 0x55)) {
        return false;
    }

    /* (2) invert the frame */
    uint8_t i0 = (uint8_t)~b0;
    uint8_t i1 = (uint8_t)~b1;
    uint8_t i2 = (uint8_t)~b2;

    /* (3) id sanity */
    uint16_t id = (uint16_t)((i0 << 8) | i1);
    if(id == 0 || id == 0xFFFF) {
        return false;
    }

    /* (4) at least one button flag set */
    int matches = 0;
    uint8_t mask = 0x01;
    for(int i = 0; i < 4; i++) {
        if(i2 & mask) matches++;
        mask <<= 2;
    }
    if(!matches) {
        return false;
    }

    out->id = id;
    out->btn_bits = i2 & 0x55;
    return true;
}

static void audiovox_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderAudiovox* instance = input->decoder;


    /* Recompute fields from the stored payload so the display is correct after
     * a load-from-file as well as after a live decode. */
    AudiovoxFrame frame = {0};
    bool ok = (instance->generic.data_count_bit == AUDIOVOX_FRAME_BITS) &&
              subghz_protocol_audiovox_parse(instance->generic.data, &frame);

    if(!ok) {
        frame.id = (uint16_t)instance->generic.serial;
        frame.btn_bits = instance->generic.btn;
    }

    /* Button names, matching the rtl_433 mask order. */
    char buttons[48] = "";
    static const char* const names[4] = {"Lock", "Unlock", "Option", "Trunk"};
    uint8_t mask = 0x01;
    bool first = true;
    for(int i = 0; i < 4; i++) {
        if(frame.btn_bits & mask) {
            if(!first) strlcat(buttons, ", ", sizeof(buttons));
            strlcat(buttons, names[i], sizeof(buttons));
            first = false;
        }
        mask <<= 2;
    }
    if(first) strlcpy(buttons, "?", sizeof(buttons));

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%07llX\r\n"
        "ID:%04X\r\n"
        "Btn:%s",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long long)(instance->generic.data & 0x1FFFFFFULL),
        frame.id,
        buttons);
}

static const SubGhzDecodeEntry audiovox_items[] = {{.name = "Audiovox PRO", .get_string = audiovox_decode}};
static const SubGhzDecodeRegistry audiovox_registry = {.items = audiovox_items, .size = 1};
static const FlipperAppPluginDescriptor decode_audiovox_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &audiovox_registry};
const FlipperAppPluginDescriptor* decode_audiovox_ep(void) { return &decode_audiovox_descriptor; }
