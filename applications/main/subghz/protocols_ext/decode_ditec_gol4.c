/* decode_ditec_gol4.fal : SD get_string for ditec_gol4 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/ditec_gol4.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <string.h>

#define GOL4_RAW_BYTES 7

struct SubGhzProtocolDecoderDitecGOL4 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_ditec_gol4_const = {
    .te_short = 400,
    .te_long = 1100,
    .te_delta = 200,
    .min_count_bit_for_found = 54,
};

static uint8_t gol4_bit_reverse(uint8_t b);
static uint8_t gol4_lcg_step(uint8_t seed, uint8_t steps);
static uint8_t gol4_lcg_inverse(uint8_t target, uint8_t steps);
static void gol4_decode_rotate_and_bitrev(uint8_t* raw);
static bool gol4_decode_lcg_xor(uint8_t* raw);
static bool gol4_rolling_decode(uint8_t* raw);
static void bits_to_raw(const uint8_t* bits, uint8_t* raw);
static uint32_t serial_to_display(const uint8_t* s);
static void subghz_protocol_ditec_gol4_decode_key(SubGhzBlockGeneric* instance);

static uint8_t gol4_bit_reverse(uint8_t b) {
    b &= 0xFF;
    uint8_t result = 0;
    for(uint8_t i = 0; i < 8; i++) {
        result = (uint8_t)((result << 1) | (b & 1));
        b >>= 1;
    }
    return result;
}

static uint8_t gol4_lcg_step(uint8_t seed, uint8_t steps) {
    uint8_t x = seed & 0xFF;
    steps &= 0xFF;
    for(uint8_t i = 0; i < steps; i++) {
        x = (uint8_t)((21 * x + 1) & 0xFF);
    }
    return x;
}

static uint8_t gol4_lcg_inverse(uint8_t target, uint8_t steps) {
    steps &= 0xFF;
    if(steps == 0) return target & 0xFF;
    return gol4_lcg_step(target, (uint8_t)(256 - steps));
}

static void gol4_decode_rotate_and_bitrev(uint8_t* raw) {
    uint8_t carry = 0;
    for(uint8_t r = 0; r < 3; r++) {
        for(uint8_t i = 2; i < 7; i++) {
            uint8_t new_carry = raw[i] & 1;
            raw[i] = (uint8_t)(((raw[i] >> 1) | (carry << 7)) & 0xFF);
            carry = new_carry;
        }
    }

    raw[0] = gol4_bit_reverse(raw[0]);
    raw[1] = gol4_bit_reverse(raw[1]);
    raw[3] = gol4_bit_reverse(raw[3]);
    raw[4] = gol4_bit_reverse(raw[4]);

    uint8_t b2 = raw[2] & 0xDF;
    b2 = (uint8_t)(((b2 << 4) | (b2 >> 4)) & 0xFF);
    b2 = (uint8_t)((~b2) & 0xFF);
    raw[2] = gol4_bit_reverse(b2);

    raw[5] = gol4_bit_reverse(raw[5]);
    raw[6] = gol4_bit_reverse(raw[6]);
}

static bool gol4_decode_lcg_xor(uint8_t* raw) {
    if(raw[6] & 0x80) raw[5] ^= 1;

    uint8_t out5 = gol4_lcg_inverse(raw[5], 0xFE);
    raw[5] = out5;

    uint8_t out6 = gol4_lcg_inverse(raw[6], raw[5]);
    raw[6] = out6;

    raw[5] ^= 0xA7;
    raw[6] ^= 0x69;
    return true;
}

static bool gol4_rolling_decode(uint8_t* raw) {
    gol4_decode_rotate_and_bitrev(raw);
    return gol4_decode_lcg_xor(raw);
}

static void bits_to_raw(const uint8_t* bits, uint8_t* raw) {
    memset(raw, 0, GOL4_RAW_BYTES);
    for(uint8_t i = 0; i < subghz_protocol_ditec_gol4_const.min_count_bit_for_found; i++) {
        uint8_t byte_idx = i / 8;
        uint8_t bit_idx = 7 - (i % 8);
        if(bits[i]) raw[byte_idx] |= (1 << bit_idx);
    }
}

static uint32_t serial_to_display(const uint8_t* s) {
    if(!s) return 0;
    return (uint32_t)((s[0] << 24) | (s[4] << 16) | (s[1] << 8) | s[3]);
}

static void subghz_protocol_ditec_gol4_decode_key(SubGhzBlockGeneric* instance) {
    uint8_t bits[subghz_protocol_ditec_gol4_const.min_count_bit_for_found];
    uint64_t data = instance->data;
    for(int i = subghz_protocol_ditec_gol4_const.min_count_bit_for_found - 1; i >= 0; i--) {
        bits[i] = (uint8_t)(data & 1);
        data >>= 1;
    }
    uint8_t decrypted[GOL4_RAW_BYTES];
    bits_to_raw(bits, decrypted);

    if(gol4_rolling_decode(decrypted)) {
        uint8_t temp_serial[5];
        memcpy(temp_serial, decrypted, 5);
        instance->serial = serial_to_display(temp_serial);
        instance->btn = decrypted[2] & 0x0F;
        instance->cnt = (uint16_t)((decrypted[5] | (decrypted[6] << 8)) & 0xFFFF);

        if(subghz_custom_btn_get_original() == 0) {
            subghz_custom_btn_set_original(instance->btn);
        }
        subghz_custom_btn_set_max(4);
    }
}

static void ditec_gol4_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderDitecGOL4* instance = input->decoder;


    subghz_protocol_ditec_gol4_decode_key(&instance->generic);

    subghz_block_generic_global.cnt_is_available = true;
    subghz_block_generic_global.cnt_length_bit = 16;
    subghz_block_generic_global.current_cnt = instance->generic.cnt;

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 4;

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "Key:0x%0lX%08lX\r\n"
        "Serial:0x%08lX\r\n"
        "Btn:%01X %s\r\n"
        "Cnt:%04lX",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)(instance->generic.data >> 32),
        (unsigned long)(instance->generic.data & 0xFFFFFFFF),
        (unsigned long)instance->generic.serial,
        instance->generic.btn,
        (instance->generic.btn == 0x0) ? "- Prog" : "",
        (unsigned long)instance->generic.cnt);
}

static const SubGhzDecodeEntry ditec_gol4_items[] = {{.name = "Ditec GOL4", .get_string = ditec_gol4_decode}};
static const SubGhzDecodeRegistry ditec_gol4_registry = {.items = ditec_gol4_items, .size = 1};
static const FlipperAppPluginDescriptor decode_ditec_gol4_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &ditec_gol4_registry};
const FlipperAppPluginDescriptor* decode_ditec_gol4_ep(void) { return &decode_ditec_gol4_descriptor; }
