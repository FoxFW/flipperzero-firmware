/* decode_honda_static.fal : SD get_string for honda_static (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/honda_static.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <string.h>
#include <lib/toolbox/level_duration.h>

#define HONDA_STATIC_SYMBOL_CAPACITY            512
#define HONDA_STATIC_SYMBOL_BYTE_COUNT          ((HONDA_STATIC_SYMBOL_CAPACITY + 7U) / 8U)

typedef struct {
    uint8_t button;
    uint32_t serial;
    uint32_t counter;
    uint8_t checksum;
} HondaStaticFields;

struct SubGhzProtocolDecoderHondaStatic {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockGeneric generic;

    uint8_t symbols[HONDA_STATIC_SYMBOL_BYTE_COUNT];
    uint16_t symbols_count;
};

static const char* const honda_static_button_names[9] = {
    "Lock",
    "Unlock",
    "Unknown",
    "Trunk",
    "Remote Start",
    "Unknown",
    "Unknown",
    "Panic",
    "Lock x2",
};

static void honda_static_u64_to_bytes_be(uint64_t data, uint8_t bytes[8]);
static const char* honda_static_button_name(uint8_t button);
static uint8_t honda_static_compact_bytes_checksum(const uint8_t compact[8]);
static void honda_static_unpack_compact(uint64_t key, HondaStaticFields* fields);
static uint8_t honda_static_ui_button(uint8_t custom, uint8_t original_btn);

static void honda_static_u64_to_bytes_be(uint64_t data, uint8_t bytes[8]) {
    for(size_t index = 0; index < 8; index++) {
        bytes[index] = (data >> ((7U - index) * 8U)) & 0xFFU;
    }
}

static const char* honda_static_button_name(uint8_t button) {
    if((button >= 1U) && (button <= COUNT_OF(honda_static_button_names))) {
        return honda_static_button_names[button - 1U];
    }
    return "Unknown";
}

static uint8_t honda_static_compact_bytes_checksum(const uint8_t compact[8]) {
    const uint8_t canonical[7] = {
        (uint8_t)((compact[0] << 4U) | (compact[1] >> 4U)),
        (uint8_t)((compact[1] << 4U) | (compact[2] >> 4U)),
        (uint8_t)((compact[2] << 4U) | (compact[3] >> 4U)),
        (uint8_t)((compact[3] << 4U) | (compact[4] >> 4U)),
        compact[5],
        compact[6],
        compact[7],
    };
    uint8_t checksum = 0U;
    for(size_t i = 0; i < COUNT_OF(canonical); i++) {
        checksum ^= canonical[i];
    }
    return checksum;
}

static void honda_static_unpack_compact(uint64_t key, HondaStaticFields* fields) {
    uint8_t compact[8];
    honda_static_u64_to_bytes_be(key, compact);

    memset(fields, 0, sizeof(*fields));
    fields->button = compact[0] & 0x0FU;
    fields->serial = ((uint32_t)compact[1] << 20U) | ((uint32_t)compact[2] << 12U) |
                     ((uint32_t)compact[3] << 4U) | ((uint32_t)compact[4] >> 4U);
    fields->counter = ((uint32_t)compact[5] << 16U) | ((uint32_t)compact[6] << 8U) |
                      (uint32_t)compact[7];
    fields->checksum = honda_static_compact_bytes_checksum(compact);
}

static uint8_t honda_static_ui_button(uint8_t custom, uint8_t original_btn) {
    switch(custom) {
    case SUBGHZ_CUSTOM_BTN_UP:
        return 0x1U;
    case SUBGHZ_CUSTOM_BTN_DOWN:
        return 0x4U;
    case SUBGHZ_CUSTOM_BTN_LEFT:
        return 0x8U;
    case SUBGHZ_CUSTOM_BTN_RIGHT:
        return 0x5U;
    case SUBGHZ_CUSTOM_BTN_OK:
    default:
        return original_btn;
    }
}

static void honda_static_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderHondaStatic* instance = input->decoder;

    HondaStaticFields decoded;
    honda_static_unpack_compact(instance->generic.data, &decoded);

    subghz_custom_btn_set_max(5);
    uint8_t display_btn = decoded.button;
    uint8_t custom_btn_id = subghz_custom_btn_get();
    if(custom_btn_id != SUBGHZ_CUSTOM_BTN_OK) {
        display_btn = honda_static_ui_button(custom_btn_id, decoded.button);
    }

    furi_string_printf(
        output,
        "%s\r\n"
        "Key:%016llX\r\n"
        "Btn:%s\r\n"
        "Ser:%07lX Cnt:%06lX",
        instance->generic.protocol_name,
        (unsigned long long)instance->generic.data,
        honda_static_button_name(display_btn),
        (unsigned long)decoded.serial,
        (unsigned long)decoded.counter);
}

static const SubGhzDecodeEntry honda_static_items[] = {{.name = "Honda Static", .get_string = honda_static_decode}};
static const SubGhzDecodeRegistry honda_static_registry = {.items = honda_static_items, .size = 1};
static const FlipperAppPluginDescriptor decode_honda_static_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &honda_static_registry};
const FlipperAppPluginDescriptor* decode_honda_static_ep(void) { return &decode_honda_static_descriptor; }
