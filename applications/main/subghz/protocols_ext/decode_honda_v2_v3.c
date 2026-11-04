/* decode_honda_v2_v3.fal : SD get_string for honda_v2_v3 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/honda_v2_v3.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>

#define HONDA_V2_TE_SHORT       96
#define HONDA_V2_TE_LONG        192
#define HONDA_V2_TE_DELTA       50
#define HONDA_V2_MIN_GAP        500
#define HONDA_V2_FRAME_BITS     148
#define HONDA_V2_MIN_FRAME_BITS 100
#define HONDA_V2_MAX_BITS       200
#define HONDA_V3_TE_SHORT       250
#define HONDA_V3_TE_LONG        500
#define HONDA_V3_TE_DELTA       80
#define HONDA_V3_MIN_GAP        1500
#define HONDA_V3_FRAME_BITS     120
#define HONDA_V3_MIN_FRAME_BITS 100
#define HONDA_V3_MAX_BITS       200
#define HONDA_VARIANT_IDX(v) ((v) == HondaVariantV2 ? 0 : 1)
#define HONDA_VARIANT_COUNT  2
#define HONDA_V2_BTN_LOCK   0x20
#define HONDA_V2_BTN_UNLOCK 0x40
#define HONDA_V2_BTN_TRUNK  0x60
#define HONDA_V3_BTN_LOCK   0x8A
#define HONDA_V3_BTN_UNLOCK 0x8C

typedef enum {
    HondaVariantV2 = 2,
    HondaVariantV3 = 3,
} HondaVariant;

typedef struct {
    HondaVariant variant;
    const SubGhzBlockConst* block_const;
    uint32_t min_gap;
    uint16_t frame_bits;
    uint16_t max_bits;
    uint8_t btn_max;
    uint8_t cnt_length_bit;
    const char* display_name;
} HondaVariantParams;

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    HondaVariant variant;
    uint8_t manchester_state;
    uint8_t bits[200];
    uint16_t bit_count;

    union {
        struct {
            uint32_t serial; // 24-bit serial
            uint8_t btn_byte1;
            uint8_t btn_byte2;
            uint16_t padding;
            uint8_t counter;
            uint32_t rolling;
            uint8_t tail;
            bool has_sync;
        } v2;
        struct {
            uint8_t sync; // should be 0x6A
            uint8_t counter;
            uint8_t payload[12]; // bytes 2-13
            uint8_t crc;
            uint16_t serial; // from Type A frames
            uint8_t btn_byte;
            uint8_t flags;
            bool is_fixed; // Type A vs Type B
        } v3;
    } extra;
} SubGhzProtocolDecoderHondaV2V3;

static const SubGhzBlockConst subghz_protocol_honda_v2_const = {
    .te_short = HONDA_V2_TE_SHORT,
    .te_long = HONDA_V2_TE_LONG,
    .te_delta = HONDA_V2_TE_DELTA,
    .min_count_bit_for_found = HONDA_V2_MIN_FRAME_BITS,
};

static const SubGhzBlockConst subghz_protocol_honda_v3_const = {
    .te_short = HONDA_V3_TE_SHORT,
    .te_long = HONDA_V3_TE_LONG,
    .te_delta = HONDA_V3_TE_DELTA,
    .min_count_bit_for_found = HONDA_V3_MIN_FRAME_BITS,
};

static const HondaVariantParams honda_variant_params[HONDA_VARIANT_COUNT] = {
    {
        .variant = HondaVariantV2,
        .block_const = &subghz_protocol_honda_v2_const,
        .min_gap = HONDA_V2_MIN_GAP,
        .frame_bits = HONDA_V2_FRAME_BITS,
        .max_bits = HONDA_V2_MAX_BITS,
        .btn_max = 3,
        .cnt_length_bit = 8,
        .display_name = "Honda V2",
    },
    {
        .variant = HondaVariantV3,
        .block_const = &subghz_protocol_honda_v3_const,
        .min_gap = HONDA_V3_MIN_GAP,
        .frame_bits = HONDA_V3_FRAME_BITS,
        .max_bits = HONDA_V3_MAX_BITS,
        .btn_max = 2,
        .cnt_length_bit = 6,
        .display_name = "Honda V3",
    },
};

static const HondaVariantParams* honda_get_params(HondaVariant v);
static const char* honda_get_button_name(HondaVariant variant, uint8_t btn);
static void honda_v2_unpack_generic(
    const SubGhzBlockGeneric* generic,
    uint32_t* serial_out,
    uint8_t* counter_out,
    uint32_t* rolling_out);
static void honda_v3_unpack_generic(
    const SubGhzBlockGeneric* generic,
    uint16_t* serial_out,
    uint8_t* counter_out,
    uint8_t* btn_byte_out,
    uint8_t* crc_out);

static const HondaVariantParams* honda_get_params(HondaVariant v) {
    return &honda_variant_params[HONDA_VARIANT_IDX(v)];
}

static const char* honda_get_button_name(HondaVariant variant, uint8_t btn) {
    switch(variant) {
    case HondaVariantV2:
        switch(btn) {
        case HONDA_V2_BTN_LOCK:
            return "LOCK";
        case HONDA_V2_BTN_UNLOCK:
            return "UNLOCK";
        case HONDA_V2_BTN_TRUNK:
            return "TRUNK";
        default: {
            static char unknown_buf[8];
            snprintf(unknown_buf, sizeof(unknown_buf), "0x%02X", btn);
            return unknown_buf;
        }
        }
    case HondaVariantV3:
        switch(btn) {
        case 0x00:
            return "Enc";
        case HONDA_V3_BTN_LOCK:
            return "LOCK";
        case HONDA_V3_BTN_UNLOCK:
            return "UNLOCK";
        case 0x8E:
            return "UNLOCK";
        case 0x80:
            return "UNLOCK";
        default: {
            static char unknown_buf_v3[8];
            snprintf(unknown_buf_v3, sizeof(unknown_buf_v3), "0x%02X", btn);
            return unknown_buf_v3;
        }
        }
    }
    return "UNKNOWN";
}

static void honda_v2_unpack_generic(
    const SubGhzBlockGeneric* generic,
    uint32_t* serial_out,
    uint8_t* counter_out,
    uint32_t* rolling_out) {
    if(serial_out) *serial_out = (generic->data >> 40) & 0xFFFFFF;
    if(counter_out) *counter_out = (generic->data >> 32) & 0xFF;
    if(rolling_out) *rolling_out = (uint32_t)(generic->data & 0xFFFFFFFF);
}

static void honda_v3_unpack_generic(
    const SubGhzBlockGeneric* generic,
    uint16_t* serial_out,
    uint8_t* counter_out,
    uint8_t* btn_byte_out,
    uint8_t* crc_out) {
    if(serial_out) *serial_out = (generic->data >> 48) & 0xFFFF;
    if(counter_out) *counter_out = (generic->data >> 32) & 0xFF;
    if(btn_byte_out) *btn_byte_out = (generic->data >> 40) & 0xFF;
    if(crc_out) *crc_out = (generic->data >> 24) & 0xFF;
}

static void honda_v2_v3_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderHondaV2V3* dec = input->decoder;

    const HondaVariantParams* params = honda_get_params(dec->variant);

    // Re-extract fields from generic.data
    switch(dec->variant) {
    case HondaVariantV2: {
        uint32_t serial;
        uint8_t counter;
        uint32_t rolling;
        honda_v2_unpack_generic(&dec->generic, &serial, &counter, &rolling);
        dec->extra.v2.serial = serial;
        dec->extra.v2.counter = counter;
        if(dec->extra.v2.rolling == 0) {
            dec->extra.v2.rolling = rolling;
        }
        dec->generic.serial = serial;
        dec->generic.btn = dec->extra.v2.btn_byte2;
        dec->generic.cnt = counter;
    } break;
    case HondaVariantV3: {
        uint16_t serial;
        uint8_t counter;
        uint8_t btn_byte;
        uint8_t crc;
        honda_v3_unpack_generic(&dec->generic, &serial, &counter, &btn_byte, &crc);
        dec->extra.v3.serial = serial;
        dec->extra.v3.counter = counter;
        dec->extra.v3.btn_byte = btn_byte;
        if(dec->extra.v3.crc == 0) {
            dec->extra.v3.crc = crc;
        }
        dec->generic.serial = serial;
        dec->generic.btn = btn_byte;
        dec->generic.cnt = counter >> 2;
    } break;
    }

    // Push UI globals
    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.btn_length_bit = 8;
    subghz_block_generic_global.current_btn = dec->generic.btn;
    subghz_block_generic_global.cnt_is_available = true;
    subghz_block_generic_global.cnt_length_bit = params->cnt_length_bit;
    subghz_block_generic_global.current_cnt = dec->generic.cnt;

    // Display with variant-specific format
    switch(dec->variant) {
    case HondaVariantV2:
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key:%08lX%08lX\r\n"
            "Sn:%06lX Btn:%s\r\n"
            "Cnt:%lu Roll:%08lX\r\n",
            params->display_name,
            dec->generic.data_count_bit,
            (unsigned long)(dec->generic.data >> 32),
            (unsigned long)(dec->generic.data & 0xFFFFFFFF),
            (unsigned long)(dec->extra.v2.serial & 0xFFFFFF),
            honda_get_button_name(dec->variant, dec->generic.btn),
            (unsigned long)dec->extra.v2.counter,
            (unsigned long)dec->extra.v2.rolling);
        break;

    case HondaVariantV3:
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key:%08lX%08lX\r\n"
            "Sn:%04lX Btn:%s %s\r\n"
            "Cnt:%lu CRC:%02X\r\n",
            params->display_name,
            dec->generic.data_count_bit,
            (unsigned long)(dec->generic.data >> 32),
            (unsigned long)(dec->generic.data & 0xFFFFFFFF),
            (unsigned long)dec->extra.v3.serial,
            honda_get_button_name(dec->variant, dec->extra.v3.btn_byte),
            dec->extra.v3.is_fixed ? "Fixed" : "Hop",
            (unsigned long)(dec->extra.v3.counter >> 2),
            dec->extra.v3.crc);
        break;
    }
}

static const SubGhzDecodeEntry honda_v2_v3_items[] = {{.name = "HondaV2&V3", .get_string = honda_v2_v3_decode}};
static const SubGhzDecodeRegistry honda_v2_v3_registry = {.items = honda_v2_v3_items, .size = 1};
static const FlipperAppPluginDescriptor decode_honda_v2_v3_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &honda_v2_v3_registry};
const FlipperAppPluginDescriptor* decode_honda_v2_v3_ep(void) { return &decode_honda_v2_v3_descriptor; }
