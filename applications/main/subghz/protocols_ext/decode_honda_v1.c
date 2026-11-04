/* decode_honda_v1.fal : SD get_string for honda_v1 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/honda_v1.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <string.h>
#include <lib/toolbox/level_duration.h>

#define HONDA_V1_BIT_COUNT             68
#define HONDA_V1_NIBBLE_MASK           0x0FU
#define HONDA_V1_SERIAL_MASK           0x0FFFFFFFU
#define HONDA_V1_COUNTER_MASK          0xFFFFU
#define HONDA_V1_LOW32_MASK            0xFFFFFFFFULL
#define HONDA_V1_BUTTON_MAX            10U
#define HONDA_V1_BUTTON_VALID_MASK     0x701U
#define HONDA_V1_BUTTON_FALLBACK_CODE  0x00088888U
#define HONDA_V1_DECODE_BUFFER_BYTES   12U

typedef enum {
    HondaV1ButtonUnlock = 0,
    HondaV1ButtonLock = 8,
    HondaV1ButtonTrunk = 9,
    HondaV1ButtonPanic = 10,
} HondaV1Button;

struct SubGhzProtocolDecoderHondaV1 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockGeneric generic;

    uint8_t step;
    uint8_t preamble_count;
    bool preamble_has_long;
    bool data_pending;
    bool last_level;
    uint8_t bits[HONDA_V1_DECODE_BUFFER_BYTES];
    uint8_t bit_count;
    uint32_t pending;
    bool pending_valid;
    uint8_t k2;
};

static const char* const honda_v1_button_names[HONDA_V1_BUTTON_MAX + 1U] = {
    [HondaV1ButtonUnlock] = "Unlock",
    [HondaV1ButtonLock] = "Lock",
    [HondaV1ButtonTrunk] = "Trunk",
    [HondaV1ButtonPanic] = "Panic",
};

static const uint32_t honda_v1_button_codes[HONDA_V1_BUTTON_MAX + 1U] = {
    [HondaV1ButtonUnlock] = 0x00080808,
    [HondaV1ButtonLock] = 0x00088888,
    [HondaV1ButtonTrunk] = 0x00099190,
    [HondaV1ButtonPanic] = 0x000FA7A0,
};

static bool honda_v1_button_valid(uint8_t b);
static const char* honda_v1_button_name(uint8_t b);
static uint32_t honda_v1_button_code(uint8_t button);
static uint8_t honda_v1_crc_fold(uint16_t v);
static uint8_t honda_v1_checksum_base(uint64_t data);
static uint8_t honda_v1_checksum_alternate(uint8_t checksum);
static void honda_v1_checksum_wire_order(uint64_t data, uint8_t* first, uint8_t* second);
static bool honda_v1_crc_valid(uint64_t data, uint8_t crc);
static void honda_v1_decode_fields(SubGhzBlockGeneric* generic);
static uint64_t honda_v1_build_key(uint32_t serial, uint8_t button, uint16_t counter);

static bool honda_v1_button_valid(uint8_t b) {
    if(b > HONDA_V1_BUTTON_MAX) return false;
    return ((HONDA_V1_BUTTON_VALID_MASK >> b) & 1U) != 0U;
}

static const char* honda_v1_button_name(uint8_t b) {
    if((b < COUNT_OF(honda_v1_button_names)) && (honda_v1_button_names[b] != NULL)) {
        return honda_v1_button_names[b];
    }
    return "Unknown";
}

static uint32_t honda_v1_button_code(uint8_t button) {
    if(!honda_v1_button_valid(button)) {
        return HONDA_V1_BUTTON_FALLBACK_CODE;
    }
    return honda_v1_button_codes[button];
}

static uint8_t honda_v1_crc_fold(uint16_t v) {
    const uint8_t lo = (uint8_t)(v & HONDA_V1_NIBBLE_MASK);
    const uint16_t hi = (uint16_t)(v >> 4U);
    int32_t s = (hi & 1U) ? (int32_t)lo : -(int32_t)lo;
    uint8_t out = (uint8_t)((s - (int32_t)hi) & 7);
    out |= (uint8_t)(((v >> 3U) & 1U) << 3U);
    if(((v >> 1U) & 1U) && (((v >> 4U) ^ (v >> 5U)) & 1U)) {
        out ^= 0x04U;
    }
    return (uint8_t)(out & HONDA_V1_NIBBLE_MASK);
}

static uint8_t honda_v1_checksum_base(uint64_t data) {
    const uint8_t a = honda_v1_crc_fold((uint16_t)(data & HONDA_V1_COUNTER_MASK));
    const uint8_t b = honda_v1_crc_fold((uint8_t)((data >> 40U) & 0xFFU));
    return (uint8_t)((a ^ b ^ 1U) & HONDA_V1_NIBBLE_MASK);
}

static uint8_t honda_v1_checksum_alternate(uint8_t checksum) {
    uint8_t mask = 0x09U;
    if((checksum & 1U) == 0U) {
        mask = (checksum & 2U) ? 0x0BU : HONDA_V1_NIBBLE_MASK;
    }
    return (uint8_t)((checksum ^ mask) & HONDA_V1_NIBBLE_MASK);
}

static void honda_v1_checksum_wire_order(uint64_t data, uint8_t* first, uint8_t* second) {
    const uint8_t checksum = honda_v1_checksum_base(data);
    const uint8_t other = honda_v1_checksum_alternate(checksum);
    if((checksum & 0x08U) != 0U) {
        *first = other;
        *second = checksum;
    } else {
        *first = checksum;
        *second = other;
    }
}

static bool honda_v1_crc_valid(uint64_t data, uint8_t crc) {
    uint8_t first = 0U;
    uint8_t second = 0U;
    honda_v1_checksum_wire_order(data, &first, &second);
    crc &= HONDA_V1_NIBBLE_MASK;
    return (crc == first) || (crc == second);
}

static void honda_v1_decode_fields(SubGhzBlockGeneric* generic) {
    const uint32_t low = (uint32_t)(generic->data & HONDA_V1_LOW32_MASK);
    generic->serial = (uint32_t)((generic->data >> 36U) & HONDA_V1_SERIAL_MASK);
    generic->btn = (uint8_t)((low >> 28U) & HONDA_V1_NIBBLE_MASK);
    generic->cnt = low & HONDA_V1_COUNTER_MASK;
    generic->data_count_bit = HONDA_V1_BIT_COUNT;
}

static uint64_t honda_v1_build_key(uint32_t serial, uint8_t button, uint16_t counter) {
    const uint32_t table = honda_v1_button_code(button);
    const uint32_t low = ((table & HONDA_V1_COUNTER_MASK) << 16U) | counter;
    const uint32_t high = ((serial & HONDA_V1_SERIAL_MASK) << 4U) | (table >> 16U);
    return ((uint64_t)high << 32U) | low;
}

static void honda_v1_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderHondaV1* instance = input->decoder;

    honda_v1_decode_fields(&instance->generic);

    subghz_custom_btn_set_max(4);
    uint8_t display_btn = (uint8_t)instance->generic.btn;
    switch(subghz_custom_btn_get()) {
    case SUBGHZ_CUSTOM_BTN_UP:
        display_btn = (uint8_t)HondaV1ButtonLock;
        break;
    case SUBGHZ_CUSTOM_BTN_DOWN:
        display_btn = (uint8_t)HondaV1ButtonUnlock;
        break;
    case SUBGHZ_CUSTOM_BTN_LEFT:
        display_btn = (uint8_t)HondaV1ButtonTrunk;
        break;
    case SUBGHZ_CUSTOM_BTN_RIGHT:
        display_btn = (uint8_t)HondaV1ButtonPanic;
        break;
    case SUBGHZ_CUSTOM_BTN_OK:
    default:
        break;
    }

    uint64_t display_data = instance->generic.data;
    uint8_t k2 = instance->k2 & HONDA_V1_NIBBLE_MASK;
    if(display_btn != (uint8_t)instance->generic.btn) {
        display_data = honda_v1_build_key(
            instance->generic.serial, display_btn, (uint16_t)instance->generic.cnt);
        uint8_t first = 0U;
        uint8_t second = 0U;
        honda_v1_checksum_wire_order(display_data, &first, &second);
        k2 = second & HONDA_V1_NIBBLE_MASK;
    }
    const bool crc_ok = honda_v1_crc_valid(display_data, k2);

    furi_string_printf(
        output,
        "%s %dbit\r\n"
        "Key:%016llX\r\n"
        "Btn:%s\r\n"
        "Sn:%07lX Cnt:%04lX\r\n"
        "Crc:%X [%s]",
        instance->generic.protocol_name,
        (int)instance->generic.data_count_bit,
        (unsigned long long)display_data,
        honda_v1_button_name(display_btn),
        (unsigned long)instance->generic.serial,
        (unsigned long)instance->generic.cnt,
        k2,
        crc_ok ? "OK" : "ERR");
}

static const SubGhzDecodeEntry honda_v1_items[] = {{.name = "Honda V1", .get_string = honda_v1_decode}};
static const SubGhzDecodeRegistry honda_v1_registry = {.items = honda_v1_items, .size = 1};
static const FlipperAppPluginDescriptor decode_honda_v1_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &honda_v1_registry};
const FlipperAppPluginDescriptor* decode_honda_v1_ep(void) { return &decode_honda_v1_descriptor; }
