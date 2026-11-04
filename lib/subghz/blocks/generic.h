#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <lib/flipper_format/flipper_format.h>
#include <furi.h>
#include <furi_hal.h>
#include "../types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SubGhzBlockGeneric SubGhzBlockGeneric;

struct SubGhzBlockGeneric {
    const char* protocol_name;
    uint64_t data;
    uint64_t data_2;
    uint32_t serial;
    uint16_t data_count_bit;
    uint8_t btn;
    uint32_t cnt;
    uint8_t cnt_2;
    uint32_t seed;
    uint8_t status;
    uint8_t crc;        // FoxFW: computed CRC for display (applab-derived decoders)
    uint8_t crc_check;   // FoxFW: 1 = CRC/validation passed
    uint8_t extra_check; // FoxFW: 1 = protocol-specific extra validation passed
};

typedef struct SubGhzBlockGenericGlobal SubGhzBlockGenericGlobal;

struct SubGhzBlockGenericGlobal {
    uint32_t current_cnt;
    uint32_t new_cnt;
    bool cnt_need_override;
    uint8_t cnt_length_bit;
    bool cnt_is_available;

    uint8_t current_btn;
    uint8_t new_btn;
    bool btn_need_override;
    uint8_t btn_length_bit;
    bool btn_is_available;

    bool endless_tx;
};

extern SubGhzBlockGenericGlobal subghz_block_generic_global;

void subghz_block_generic_global_counter_override_set(uint32_t counter);

bool subghz_block_generic_global_counter_override_get(uint32_t* counter);

void subghz_block_generic_global_button_override_set(uint8_t button);

bool subghz_block_generic_global_button_override_get(uint8_t* button);

void subghz_block_generic_global_reset(void* p);

void subghz_block_generic_get_preset_name(const char* preset_name, FuriString* preset_str);

SubGhzProtocolStatus subghz_block_generic_serialize(
    SubGhzBlockGeneric* instance,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_block_generic_deserialize(SubGhzBlockGeneric* instance, FlipperFormat* flipper_format);

SubGhzProtocolStatus subghz_block_generic_deserialize_check_count_bit(
    SubGhzBlockGeneric* instance,
    FlipperFormat* flipper_format,
    uint16_t count_bit);

void subghz_block_generic_advance_counter(uint32_t* cnt, uint32_t max);
void subghz_block_generic_global_publish_cnt(uint32_t cnt, uint8_t length_bit);
void subghz_block_generic_global_publish_btn(uint8_t btn, uint8_t length_bit);

bool subghz_block_read_hex(FlipperFormat* flipper_format, const char* key, void* data, size_t size);
bool subghz_block_write_hex(FlipperFormat* flipper_format, const char* key, uint64_t data, size_t size);
void subghz_block_write_disp(
    FlipperFormat* flipper_format, const char* protocol_name, const char* button_name);

#ifdef __cplusplus
}
#endif
