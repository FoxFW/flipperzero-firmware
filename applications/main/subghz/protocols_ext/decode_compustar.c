/* decode_compustar.fal : SD get_string for compustar (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/compustar.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define COMPUSTAR_BIT_COUNT 36

struct SubGhzProtocolDecoderCompustar {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

typedef struct {
    const char* name;
    uint8_t len;
    const uint8_t* vals;
} CompustarButton;

static const uint8_t compustar_btn_lock[] =
    {0x03, 0x05, 0x09, 0x0b, 0x0d, 0x0f, 0x1f, 0x17, 0x13, 0x15, 0x19, 0x1b, 0x1d};

static const uint8_t compustar_btn_panic[] = {0x18};

static const uint8_t compustar_btn_start[] = {0x09, 0x0a, 0x0c, 0x0b, 0x0e, 0x0d, 0x04, 0x1f,
                                              0x08, 0x19, 0x1a, 0x1c, 0x1b, 0x1e, 0x1d, 0x12};

static const uint8_t compustar_btn_trunk[] =
    {0x05, 0x06, 0x0c, 0x0e, 0x0d, 0x1f, 0x17, 0x02, 0x15, 0x16, 0x1c, 0x1e, 0x1d, 0x08, 0x14};

static const uint8_t compustar_btn_unlock[] =
    {0x03, 0x06, 0x0a, 0x0b, 0x0e, 0x1f, 0x07, 0x17, 0x13, 0x16, 0x1a, 0x1b, 0x1e};

static const uint8_t compustar_btn_longpress[] = {
    0x23, 0x31, 0x13, 0x16, 0x17, 0x1a, 0x1b, 0x1e, 0x15, 0x16, 0x1c, 0x1e, 0x1d, 0x08,
    0x14, 0x08, 0x19, 0x1a, 0x1c, 0x1b, 0x1e, 0x1d, 0x12, 0x13, 0x15, 0x19, 0x1b, 0x1d};

static const CompustarButton compustar_button_map[6] = {
    {"Lock", sizeof(compustar_btn_lock), compustar_btn_lock},
    {"Panic", sizeof(compustar_btn_panic), compustar_btn_panic},
    {"Start", sizeof(compustar_btn_start), compustar_btn_start},
    {"Trunk", sizeof(compustar_btn_trunk), compustar_btn_trunk},
    {"Unlock", sizeof(compustar_btn_unlock), compustar_btn_unlock},
    {"Long Press", sizeof(compustar_btn_longpress), compustar_btn_longpress},
};

static void subghz_protocol_compustar_append_button(FuriString* output, uint8_t button);
static bool subghz_protocol_compustar_check(SubGhzBlockGeneric* instance);

static void subghz_protocol_compustar_append_button(FuriString* output, uint8_t button) {
    uint8_t code = button & 0x7f;
    int matches = 0;
    for(int i = 0; i < 6; i++) {
        for(int j = 0; j < compustar_button_map[i].len; j++) {
            if(code == compustar_button_map[i].vals[j]) {
                if(matches) furi_string_cat_printf(output, "; ");
                furi_string_cat_printf(output, "%s", compustar_button_map[i].name);
                matches++;
                break;
            }
        }
    }
    if(!matches) furi_string_cat_printf(output, "?");
    if(button & 0x80) furi_string_cat_printf(output, "; Secondary Mode");
}

static bool subghz_protocol_compustar_check(SubGhzBlockGeneric* instance) {
    if(instance->data_count_bit != COMPUSTAR_BIT_COUNT) return false;
    uint64_t data = instance->data & 0xFFFFFFFFFULL; /* 36 bits */

    uint16_t id = (uint16_t)((data >> 20) & 0xFFFF);
    if(id == 0x0000 || id == 0xFFFF) return false;

    /* 3-bit unknown field must be 0b111 */
    if(((data >> 17) & 0x7) != 0x7) return false;

    uint8_t button_inverse = (uint8_t)((data >> 9) & 0xFF);
    uint8_t button = (uint8_t)((data >> 1) & 0xFF);

    /* MIC: inverted button field must be the bitwise NOT of the button field */
    if((uint8_t)(~button_inverse) != button) return false;

    instance->serial = id;
    instance->btn = button;
    instance->cnt = 0; /* fixed code, no rolling counter */
    return true;
}

static void compustar_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderCompustar* instance = input->decoder;


    subghz_protocol_compustar_check(&instance->generic);

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%09llX\r\n"
        "Id:0x%04lX  Btn:0x%02X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long long)(instance->generic.data & 0xFFFFFFFFFULL),
        (unsigned long)(instance->generic.serial & 0xFFFF),
        instance->generic.btn);

    subghz_protocol_compustar_append_button(output, instance->generic.btn);
}

static const SubGhzDecodeEntry compustar_items[] = {{.name = "Compustar", .get_string = compustar_decode}};
static const SubGhzDecodeRegistry compustar_registry = {.items = compustar_items, .size = 1};
static const FlipperAppPluginDescriptor decode_compustar_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &compustar_registry};
const FlipperAppPluginDescriptor* decode_compustar_ep(void) { return &decode_compustar_descriptor; }
