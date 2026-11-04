/* decode_astrostart.fal : SD get_string for astrostart (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/astrostart.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

struct SubGhzProtocolDecoderAstrostart {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static bool subghz_protocol_astrostart_check(
    uint64_t data,
    uint32_t* serial,
    uint8_t* btn,
    uint8_t* crc);
static const char* subghz_protocol_astrostart_btn_name(uint8_t btn);

static bool subghz_protocol_astrostart_check(
    uint64_t data,
    uint32_t* serial,
    uint8_t* btn,
    uint8_t* crc) {
    uint8_t bytes[7];
    bytes[0] = (uint8_t)(data >> 44); /* button          bits 0..7   */
    bytes[1] = (uint8_t)(data >> 36); /* ~button         bits 8..15  */
    bytes[2] = (uint8_t)(data >> 28); /* id[31..24]      bits 16..23 */
    bytes[3] = (uint8_t)(data >> 20); /* id[23..16]      bits 24..31 */
    bytes[4] = (uint8_t)(data >> 12); /* id[15..8]       bits 32..39 */
    bytes[5] = (uint8_t)(data >> 4); /* id[7..0]        bits 40..47 */
    uint8_t rx_checksum = (uint8_t)(data & 0x0F); /* checksum bits 48..51 */

    /* 1) button must be the bitwise inverse of the next byte */
    if(bytes[0] != (uint8_t)(~bytes[1] & 0xFF)) {
        return false;
    }

    /* 2) nibble-sum checksum over the four ID bytes */
    uint8_t expected = 0;
    for(int i = 2; i < 6; i++) {
        expected = (expected + (bytes[i] >> 4)) & 0x0F;
        expected = (expected + bytes[i]) & 0x0F;
    }
    if(expected != rx_checksum) {
        return false;
    }

    *serial = ((uint32_t)bytes[2] << 24) | ((uint32_t)bytes[3] << 16) |
              ((uint32_t)bytes[4] << 8) | (uint32_t)bytes[5];
    *btn = bytes[0];
    *crc = rx_checksum;
    return true;
}

static const char* subghz_protocol_astrostart_btn_name(uint8_t btn) {
    static const struct {
        const char* name;
        uint8_t len;
        uint8_t vals[6];
    } map[] = {
        {"Lock", 6, {0x2b, 0x03, 0x27, 0x0f, 0x35, 0x37}},
        {"Panic", 6, {0x1f, 0x35, 0x0d, 0x25, 0x15, 0x2d}},
        {"Start", 6, {0x13, 0x37, 0x2d, 0x33, 0x3d, 0x3b}},
        {"Stop", 6, {0x2f, 0x0d, 0x33, 0x03, 0x1d, 0x17}},
        {"Trunk", 6, {0x23, 0x25, 0x3d, 0x1d, 0x27, 0x07}},
        {"Unlock", 6, {0x0b, 0x15, 0x3b, 0x17, 0x07, 0x0f}},
        {"Multiple", 1, {0x3f, 0, 0, 0, 0, 0}},
    };
    for(size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        for(uint8_t j = 0; j < map[i].len; j++) {
            if(btn == map[i].vals[j]) return map[i].name;
        }
    }
    return "?";
}

static void astrostart_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderAstrostart* instance = input->decoder;


    /* Recover fields for display (also covers loaded/deserialized frames). */
    uint32_t serial = instance->generic.serial;
    uint8_t btn = instance->generic.btn;
    uint8_t crc = instance->generic.crc;
    subghz_protocol_astrostart_check(instance->generic.data, &serial, &btn, &crc);

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%013llX\r\n"
        "ID:%08lX\r\n"
        "Btn:%02X (%s)  CRC:%X",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long long)instance->generic.data,
        (unsigned long)serial,
        btn,
        subghz_protocol_astrostart_btn_name(btn),
        crc);
}

static const SubGhzDecodeEntry astrostart_items[] = {{.name = "AstroStart 2000", .get_string = astrostart_decode}};
static const SubGhzDecodeRegistry astrostart_registry = {.items = astrostart_items, .size = 1};
static const FlipperAppPluginDescriptor decode_astrostart_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &astrostart_registry};
const FlipperAppPluginDescriptor* decode_astrostart_ep(void) { return &decode_astrostart_descriptor; }
