
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/telecrane.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

typedef struct {
    const char* name;
    uint16_t mask;
} TelecraneCmd;

static const TelecraneCmd telecrane_cmds[] = {
    {"START", 0x0010}, {"STOP", 0x0008}, {"UP", 0x0004},   {"DOWN", 0x0002},
    {"NORTH", 0x2000}, {"SOUTH", 0x4000}, {"EAST", 0x0001}, {"WEST", 0x8000},
    {"AUX1", 0x1010},  {"AUX2", 0x0808},  {"AUX3", 0x0404}, {"AUX4", 0x0202},
};

struct SubGhzProtocolDecoderTelecrane {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint64_t sync_sr;
    uint64_t collect;
    uint8_t collect_bits;
    bool collecting;

    uint16_t cmd_mask;
    uint8_t channel;
};

static void telecrane_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderTelecrane* instance = input->decoder;

    const uint32_t id = (uint32_t)(instance->generic.data & 0xFFFFFF);
    const uint32_t b0 = (id >> 16) & 0xFF;
    const uint32_t b1 = (id >> 8) & 0xFF;
    const uint32_t b2 = id & 0xFF;
    const uint8_t ch = instance->channel;
    const char ch_nibble = (ch <= 9) ? (char)('0' + ch) : (char)('A' + ch - 10);

    const char* cmd_name = "?";
    for(size_t i = 0; i < sizeof(telecrane_cmds) / sizeof(telecrane_cmds[0]); i++) {
        if(telecrane_cmds[i].mask == instance->cmd_mask) {
            cmd_name = telecrane_cmds[i].name;
            break;
        }
    }

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Serial:C%c%02lu%02lu%02lu\r\n"
        "ID:%06lX  Ch:%u\r\n"
        "Cmd:%s (0x%04X)",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        ch_nibble,
        (unsigned long)b0,
        (unsigned long)b1,
        (unsigned long)b2,
        (unsigned long)id,
        ch,
        cmd_name,
        instance->cmd_mask);
}

static const SubGhzDecodeEntry telecrane_items[] = {
    {.name = "Telecrane", .get_string = telecrane_decode}};
static const SubGhzDecodeRegistry telecrane_registry = {
    .items = telecrane_items,
    .size = 1};
static const FlipperAppPluginDescriptor decode_telecrane_descriptor = {
    .appid = SUBGHZ_DECODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION,
    .entry_point = &telecrane_registry};
const FlipperAppPluginDescriptor* decode_telecrane_ep(void) {
    return &decode_telecrane_descriptor;
}
