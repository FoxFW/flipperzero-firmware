
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/bmw_v4.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>

#define BMW_TE_US             62
#define BMW_TE1_MIN_US        40
#define BMW_TE1_MAX_US        90
#define BMW_TE2_MIN_US        100
#define BMW_TE2_MAX_US        160
#define BMW_GAP_US            400
#define BMW_PREAMBLE_MIN      32
#define BMW_PREAMBLE_MAX      280
#define BMW_PREAMBLE_BITS     212
#define BMW_SYNC              0x599EU
#define BMW_SYNC_INV          0xA661U
#define BMW_HEADER_HUNT_MAX   40
#define BMW_PAYLOAD_BITS      128
#define BMW_FIXED_VALUE       0x89U
#define BMW_CNT_BITS          17
#define BMW_CNT_MASK          0x1FFFFU
#define BMW_HOP_MASK          0x7FFFFFFFFFFFULL
#define BMW_BTN_TEMPLATE      0x01U
#define BMW_BTN_TEMPLATE_MASK 0xE7U
#define BMW_BTN_XX_MASK       0x3U
#define BMW_BTN_UNLOCK        0x1U
#define BMW_BTN_LOCK          0x2U
#define BMW_BTN_TRUNK         0x3U
#define BMW_BTN_MAX           3
#define BMW_REPEAT_GAP_US     200000
#define BMW_REPEAT_DEFAULT    3
#define BMW_UPLOAD_CAPACITY   2048

struct SubGhzProtocolDecoderBmwV4 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint64_t decode_head;
    uint16_t preamble_count;
    uint16_t header_bits;
    uint16_t sync_reg;
    int8_t pair_chip;
    bool invert;
};

static uint8_t bmw_v4_wire_xx(uint8_t btn_byte) {
    return (uint8_t)((btn_byte >> 3) & BMW_BTN_XX_MASK);
}

static uint32_t bmw_v4_wire_cnt(uint64_t lo) {
    return (uint32_t)((((lo >> 48) & 0xFFFFU) << 1) | ((lo >> 47) & 1U));
}

static const char* bmw_v4_btn_name(uint8_t btn) {
    switch(btn) {
    case BMW_BTN_UNLOCK:
        return "Unlock";
    case BMW_BTN_LOCK:
        return "Lock";
    case BMW_BTN_TRUNK:
        return "Trunk";
    default:
        return "?";
    }
}

static void bmw_v4_parse_fields(SubGhzBlockGeneric* instance) {
    const uint64_t hi = instance->data;
    const uint64_t lo = instance->data_2;
    const uint8_t btn_byte = (uint8_t)((hi >> 8) & 0xFFU);

    instance->serial = (uint32_t)(hi >> 16);
    instance->btn = bmw_v4_wire_xx(btn_byte);
    instance->cnt = bmw_v4_wire_cnt(lo) & BMW_CNT_MASK;
}

static uint8_t bmw_v4_custom_id_from_code(uint8_t code) {
    switch(code) {
    case BMW_BTN_LOCK: return SUBGHZ_CUSTOM_BTN_UP;
    case BMW_BTN_UNLOCK: return SUBGHZ_CUSTOM_BTN_DOWN;
    case BMW_BTN_TRUNK: return SUBGHZ_CUSTOM_BTN_LEFT;
    default: return SUBGHZ_CUSTOM_BTN_OK;
    }
}

static void bmw_v4_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderBmwV4* instance = input->decoder;
    bmw_v4_parse_fields(&instance->generic);

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.cnt_is_available = true;
    subghz_block_generic_global.btn_length_bit = 2;
    subghz_block_generic_global.cnt_length_bit = BMW_CNT_BITS;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.current_cnt = instance->generic.cnt;
    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(bmw_v4_custom_id_from_code(instance->generic.btn));
    }
    subghz_custom_btn_set_max(BMW_BTN_MAX);

    furi_string_cat_printf(
        output,
        "%s 128bit\r\n"
        "Sn:%08lX\r\n"
        "Btn:%s Cnt:%u\r\n"
        "Hop:%012llX",
        instance->generic.protocol_name,
        (unsigned long)instance->generic.serial,
        bmw_v4_btn_name(instance->generic.btn),
        (unsigned int)instance->generic.cnt,
        (unsigned long long)(instance->generic.data_2 & BMW_HOP_MASK));
}

static const SubGhzDecodeEntry bmw_v4_items[] = {{.name = SUBGHZ_PROTOCOL_BMW_V4_NAME, .get_string = bmw_v4_decode}};
static const SubGhzDecodeRegistry bmw_v4_registry = {.items = bmw_v4_items, .size = 1};
static const FlipperAppPluginDescriptor decode_bmw_v4_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &bmw_v4_registry};
const FlipperAppPluginDescriptor* decode_bmw_v4_ep(void) { return &decode_bmw_v4_descriptor; }
