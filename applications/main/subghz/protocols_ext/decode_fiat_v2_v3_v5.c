/* decode_fiat_v2_v3_v5.fal : SD get_string for fiat_v2_v3_v5 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/fiat_v2_v3_v5.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>

#define FIAT_V3_TE_SHORT         460
#define FIAT_V3_TE_LONG          920
#define FIAT_V3_TE_DELTA         200
#define FIAT_V3_MIN_GAP          1500
#define FIAT_V3_FRAME_BITS       70
#define FIAT_V3_MIN_FRAME_BITS   60
#define FIAT_V3_MAX_BITS         100
#define FIAT_V3_PREAMBLE_BITS    13
#define FIAT_V5_TE_SHORT       200
#define FIAT_V5_TE_LONG        400
#define FIAT_V5_TE_DELTA       100
#define FIAT_V5_MIN_GAP        2000
#define FIAT_V5_FRAME_BITS     97
#define FIAT_V5_MIN_FRAME_BITS 90
#define FIAT_V5_MAX_BITS       130
#define FIAT_V5_PREAMBLE_BITS  15
#define FIAT_VARIANT_IDX(v) ((v) == FiatVariantV3 ? 0 : 1)
#define FIAT_VARIANT_COUNT  2
#define FIAT_V3_BTN_LOCK   0x00
#define FIAT_V3_BTN_UNLOCK 0x01
#define FIAT_V3_BTN_TRUNK  0x02
#define FIAT_V5_BTN_OPEN  0x80
#define FIAT_V5_BTN_TRUNK 0x40
#define FIAT_V5_BTN_CLOSE 0x20

typedef enum {
    FiatVariantV3 = 3,
    FiatVariantV5 = 5,
} FiatVariant;

typedef struct {
    FiatVariant variant;
    const SubGhzBlockConst* block_const;
    uint32_t min_gap;
    uint16_t frame_bits;
    uint16_t max_bits;
    uint8_t preamble_bits;
    uint8_t btn_max;
    bool has_counter;
    uint8_t cnt_length_bit;
    uint8_t btn_length_bit;
    const char* display_name;
} FiatVariantParams;

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    FiatVariant variant;
    uint8_t manchester_state;
    uint8_t bits[150];
    uint16_t bit_count;

    union {
        struct {
            uint32_t fix_value;
            uint8_t btn;
            uint8_t status_bits;
            uint64_t hop_value;
            uint8_t hop_low;
            uint16_t timing_count;
            uint32_t preamble_gap_sum;
            uint16_t preamble_gap_count;
            bool preamble_ended;
        } v3;
        struct {
            uint32_t fix_value;
            uint8_t type_byte;
            uint8_t btn;
            uint8_t seed;
            uint8_t cnt;
            uint32_t hop_value;
        } v5;
    } extra;
} SubGhzProtocolDecoderFiat;

static const SubGhzBlockConst subghz_protocol_fiat_v3_const = {
    .te_short = FIAT_V3_TE_SHORT,
    .te_long = FIAT_V3_TE_LONG,
    .te_delta = FIAT_V3_TE_DELTA,
    .min_count_bit_for_found = FIAT_V3_MIN_FRAME_BITS,
};

static const SubGhzBlockConst subghz_protocol_fiat_v5_const = {
    .te_short = FIAT_V5_TE_SHORT,
    .te_long = FIAT_V5_TE_LONG,
    .te_delta = FIAT_V5_TE_DELTA,
    .min_count_bit_for_found = FIAT_V5_MIN_FRAME_BITS,
};

static const FiatVariantParams fiat_variant_params[FIAT_VARIANT_COUNT] = {
    {
        .variant = FiatVariantV3,
        .block_const = &subghz_protocol_fiat_v3_const,
        .min_gap = FIAT_V3_MIN_GAP,
        .frame_bits = FIAT_V3_FRAME_BITS,
        .max_bits = FIAT_V3_MAX_BITS,
        .preamble_bits = FIAT_V3_PREAMBLE_BITS,
        .btn_max = 3,
        .has_counter = false,
        .cnt_length_bit = 0,
        .btn_length_bit = 2,
        .display_name = "Fiat V3",
    },
    {
        .variant = FiatVariantV5,
        .block_const = &subghz_protocol_fiat_v5_const,
        .min_gap = FIAT_V5_MIN_GAP,
        .frame_bits = FIAT_V5_FRAME_BITS,
        .max_bits = FIAT_V5_MAX_BITS,
        .preamble_bits = FIAT_V5_PREAMBLE_BITS,
        .btn_max = 3,
        .has_counter = true,
        .cnt_length_bit = 8,
        .btn_length_bit = 8,
        .display_name = "Fiat V5",
    },
};

static const uint8_t fiat_v3_btns[] = {FIAT_V3_BTN_LOCK, FIAT_V3_BTN_UNLOCK, FIAT_V3_BTN_TRUNK};

static const uint8_t fiat_v5_btns[] = {FIAT_V5_BTN_CLOSE, FIAT_V5_BTN_OPEN, FIAT_V5_BTN_TRUNK};

static FiatVariant fiat_current_variant = FiatVariantV3;

static void subghz_custom_btn_fill_labels_positional(
    uint8_t btn,
    const uint8_t* btns,
    size_t btns_count,
    const char* (*get_name)(uint8_t));
static const FiatVariantParams* fiat_get_params(FiatVariant v);
static const char* fiat_get_button_name_simple(uint8_t btn);
static const char* fiat_get_button_name(FiatVariant variant, uint8_t btn);
static uint8_t fiat_v3_hop_btn_extract(uint64_t hop);
static bool fiat_v3_hop_invariants_check(uint64_t hop);
static void fiat_v3_unpack_generic(
    const SubGhzBlockGeneric* generic,
    uint8_t hop_low,
    uint32_t* fix_out,
    uint8_t* btn_out,
    uint64_t* hop_out);
static void fiat_v5_unpack_generic(
    const SubGhzBlockGeneric* generic,
    uint32_t* fix_out,
    uint32_t* hop_out);

static void subghz_custom_btn_fill_labels_positional(
    uint8_t btn,
    const uint8_t* btns,
    size_t btns_count,
    const char* (*get_name)(uint8_t)) {
    (void)btn;
    (void)btns;
    (void)btns_count;
    (void)get_name;
}

static const FiatVariantParams* fiat_get_params(FiatVariant v) {
    return &fiat_variant_params[FIAT_VARIANT_IDX(v)];
}

static const char* fiat_get_button_name_simple(uint8_t btn) {
    return fiat_get_button_name(fiat_current_variant, btn);
}

static const char* fiat_get_button_name(FiatVariant variant, uint8_t btn) {
    switch(variant) {
    case FiatVariantV3:
        switch(btn) {
        case FIAT_V3_BTN_LOCK:
            return "LOCK";
        case FIAT_V3_BTN_UNLOCK:
            return "UNLOCK";
        case FIAT_V3_BTN_TRUNK:
            return "TRUNK";
        default:
            return "UNKNOWN";
        }
    case FiatVariantV5:
        switch(btn) {
        case FIAT_V5_BTN_OPEN:
            return "OPEN";
        case FIAT_V5_BTN_TRUNK:
            return "TRUNK";
        case FIAT_V5_BTN_CLOSE:
            return "CLOSE";
        default:
            return "UNKNOWN";
        }
    }
    return "UNKNOWN";
}

static uint8_t fiat_v3_hop_btn_extract(uint64_t hop) {
    /* Mask selects bit-pairs (4,5) in each of the 5 full bytes */
    const uint64_t mask = (3ULL << 4) | (3ULL << 12) | (3ULL << 20) | (3ULL << 28) | (3ULL << 36);
    uint64_t masked = hop & mask;

    /* popcount parity: XOR of all selected bits = popcount mod 2 */
    masked ^= masked >> 32;
    masked ^= masked >> 16;
    masked ^= masked >> 8;
    masked ^= masked >> 4;
    masked ^= masked >> 2;
    masked ^= masked >> 1;
    return (uint8_t)(masked & 1);
}

static bool fiat_v3_hop_invariants_check(uint64_t hop) {
    /* R1: XOR bits {5,6,13,14,21,22,29,30,37,38} = 0 */
    const uint64_t r1 = (3ULL << 5) | (3ULL << 13) | (3ULL << 21) | (3ULL << 29) | (3ULL << 37);
    /* R2: XOR bits {5,7,13,15,21,23,29,31,37,39} = 0 */
    const uint64_t r2 = (5ULL << 5) | (5ULL << 13) | (5ULL << 21) | (5ULL << 29) | (5ULL << 37);
    /* R3: XOR bits {5,8,13,16,21,24,29,32,37,40} = 0 */
    const uint64_t r3 = (1ULL << 5) | (1ULL << 8) | (1ULL << 13) | (1ULL << 16) |
                         (1ULL << 21) | (1ULL << 24) | (1ULL << 29) | (1ULL << 32) |
                         (1ULL << 37) | (1ULL << 40);
    /* R4: XOR bits {1,9,17,25,33,41} = 0 */
    const uint64_t r4 = (1ULL << 1) | (1ULL << 9) | (1ULL << 17) | (1ULL << 25) |
                         (1ULL << 33) | (1ULL << 41);

    const uint64_t masks[] = {r1, r2, r3, r4};
    for(int i = 0; i < 4; i++) {
        uint64_t v = hop & masks[i];
        v ^= v >> 32;
        v ^= v >> 16;
        v ^= v >> 8;
        v ^= v >> 4;
        v ^= v >> 2;
        v ^= v >> 1;
        if(v & 1) return false;
    }
    return true;
}

static void fiat_v3_unpack_generic(
    const SubGhzBlockGeneric* generic,
    uint8_t hop_low,
    uint32_t* fix_out,
    uint8_t* btn_out,
    uint64_t* hop_out) {
    uint32_t fix = (generic->data >> 42) & 0x3FFFFF;
    uint8_t btn = (generic->data >> 40) & 0x3;
    uint64_t hop = ((generic->data & 0xFFFFFFFFFFULL) << 4) | (hop_low & 0xF);
    if(fix_out) *fix_out = fix;
    if(btn_out) *btn_out = btn;
    if(hop_out) *hop_out = hop;
}

static void fiat_v5_unpack_generic(
    const SubGhzBlockGeneric* generic,
    uint32_t* fix_out,
    uint32_t* hop_out) {
    if(fix_out) *fix_out = (uint32_t)(generic->data >> 32);
    if(hop_out) *hop_out = (uint32_t)(generic->data & 0xFFFFFFFF);
}

static void fiat_v2_v3_v5_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderFiat* dec = input->decoder;

    const FiatVariantParams* params = fiat_get_params(dec->variant);

    // Re-extract fields from generic.data
    switch(dec->variant) {
    case FiatVariantV3: {
        uint32_t fix;
        uint8_t btn;
        uint64_t hop;
        fiat_v3_unpack_generic(&dec->generic, dec->extra.v3.hop_low, &fix, &btn, &hop);
        dec->extra.v3.fix_value = fix;
        dec->extra.v3.btn = btn;
        dec->extra.v3.hop_value = hop;
        dec->generic.serial = fix;
        dec->generic.btn = btn;

        uint8_t hop_btn = fiat_v3_hop_btn_extract(hop);
        dec->generic.extra_check = (hop_btn == btn);
        dec->generic.crc_check = fiat_v3_hop_invariants_check(hop);
    } break;
    case FiatVariantV5:
        fiat_v5_unpack_generic(&dec->generic, &dec->extra.v5.fix_value, &dec->extra.v5.hop_value);
        dec->extra.v5.btn = dec->generic.btn;
        dec->extra.v5.cnt = dec->generic.cnt;
        dec->generic.serial = dec->extra.v5.fix_value;
        break;
    }

    // Push UI globals
    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.btn_length_bit = params->btn_length_bit;
    subghz_block_generic_global.current_btn = dec->generic.btn;
    fiat_current_variant = dec->variant;
    switch(dec->variant) {
    case FiatVariantV3:
        subghz_custom_btn_fill_labels_positional(
            dec->generic.btn, fiat_v3_btns, COUNT_OF(fiat_v3_btns), fiat_get_button_name_simple);
        break;
    case FiatVariantV5:
        subghz_custom_btn_fill_labels_positional(
            dec->generic.btn, fiat_v5_btns, COUNT_OF(fiat_v5_btns), fiat_get_button_name_simple);
        break;
    }
    subghz_block_generic_global.cnt_is_available = params->has_counter;
    if(params->has_counter) {
        subghz_block_generic_global.cnt_length_bit = params->cnt_length_bit;
        subghz_block_generic_global.current_cnt = dec->generic.cnt;
    }

    // Display with variant-specific name
    switch(dec->variant) {
    case FiatVariantV3:
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key:%016llX\r\n"
            "Fix:%06lX Bt:%02X [%s]\r\n"
            "Hop:%011llX\r\n"
            "HpBt:%s Inv:%s\r\n",
            params->display_name,
            dec->generic.data_count_bit,
            dec->generic.data,
            dec->extra.v3.fix_value,
            dec->extra.v3.btn,
            fiat_get_button_name(dec->variant, dec->extra.v3.btn),
            dec->extra.v3.hop_value,
            dec->generic.extra_check ? "OK" : "ERR",
            dec->generic.crc_check ? "OK" : "ERR");
        break;

    case FiatVariantV5:
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key:%016llX\r\n"
            "Fix:%08lX Btn: %02X [%s]\r\n"
            "Hop:%08lX Cnt:%u\r\n"
            "Type:%02X Seed:%02X\r\n",
            params->display_name,
            dec->generic.data_count_bit,
            dec->generic.data,
            dec->extra.v5.fix_value,
            dec->extra.v5.btn,
            fiat_get_button_name(dec->variant, dec->extra.v5.btn),
            dec->extra.v5.hop_value,
            dec->extra.v5.cnt,
            dec->extra.v5.type_byte,
            dec->extra.v5.seed);
        break;
    }
}

static const SubGhzDecodeEntry fiat_v2_v3_v5_items[] = {{.name = "Fiat Multi", .get_string = fiat_v2_v3_v5_decode}};
static const SubGhzDecodeRegistry fiat_v2_v3_v5_registry = {.items = fiat_v2_v3_v5_items, .size = 1};
static const FlipperAppPluginDescriptor decode_fiat_v2_v3_v5_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &fiat_v2_v3_v5_registry};
const FlipperAppPluginDescriptor* decode_fiat_v2_v3_v5_ep(void) { return &decode_fiat_v2_v3_v5_descriptor; }
