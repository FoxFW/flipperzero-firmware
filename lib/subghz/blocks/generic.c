#include "generic.h"
#include <lib/toolbox/stream/stream.h>
#include <lib/flipper_format/flipper_format_i.h>

#define TAG "SubGhzBlockGeneric"

SubGhzBlockGenericGlobal subghz_block_generic_global;

void subghz_block_generic_global_counter_override_set(uint32_t counter) {
    subghz_block_generic_global.new_cnt = counter;
    subghz_block_generic_global.cnt_need_override = true;
}

bool subghz_block_generic_global_counter_override_get(uint32_t* counter) {

    if(subghz_block_generic_global.cnt_need_override) {
        *counter = subghz_block_generic_global.new_cnt &
                   ((0xFFFFFFFF >> (32 - subghz_block_generic_global.cnt_length_bit)));
        subghz_block_generic_global.cnt_need_override = false;
        return true;
    } else {
        return false;
    }
}

void subghz_block_generic_global_button_override_set(uint8_t button) {
    subghz_block_generic_global.new_btn = button;
    subghz_block_generic_global.btn_need_override = true;
}

bool subghz_block_generic_global_button_override_get(uint8_t* button) {

    if(subghz_block_generic_global.btn_need_override) {
        *button = subghz_block_generic_global.new_btn &
                  ((0xFF >> (8 - subghz_block_generic_global.btn_length_bit)));
        subghz_block_generic_global.btn_need_override = false;
        return true;
    } else {
        return false;
    }
}

void subghz_block_generic_global_reset(void* p) {
    UNUSED(p);

    bool tmp = subghz_block_generic_global.endless_tx;
    memset(&subghz_block_generic_global, 0, sizeof(subghz_block_generic_global));
    subghz_block_generic_global.endless_tx = tmp;
}

void subghz_block_generic_get_preset_name(const char* preset_name, FuriString* preset_str) {
    const char* preset_name_temp;
    if(!strcmp(preset_name, "AM270")) {
        preset_name_temp = "FuriHalSubGhzPresetOok270Async";
    } else if(!strcmp(preset_name, "AM650")) {
        preset_name_temp = "FuriHalSubGhzPresetOok650Async";
    } else if(!strcmp(preset_name, "FM238")) {
        preset_name_temp = "FuriHalSubGhzPreset2FSKDev238Async";
    } else if(!strcmp(preset_name, "FM12K")) {
        preset_name_temp = "FuriHalSubGhzPreset2FSKDev12KAsync";
    } else if(!strcmp(preset_name, "FM476")) {
        preset_name_temp = "FuriHalSubGhzPreset2FSKDev476Async";
    } else {
        preset_name_temp = "FuriHalSubGhzPresetCustom";
    }
    furi_string_set(preset_str, preset_name_temp);
}

SubGhzProtocolStatus subghz_block_generic_serialize(
    SubGhzBlockGeneric* instance,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(instance);
    SubGhzProtocolStatus res = SubGhzProtocolStatusError;
    FuriString* temp_str;
    temp_str = furi_string_alloc();
    do {
        stream_clean(flipper_format_get_raw_stream(flipper_format));
        if(!flipper_format_write_header_cstr(
               flipper_format, SUBGHZ_KEY_FILE_TYPE, SUBGHZ_KEY_FILE_VERSION)) {
            FURI_LOG_E(TAG, "Unable to add header");
            res = SubGhzProtocolStatusErrorParserHeader;
            break;
        }

        if(!flipper_format_write_uint32(flipper_format, "Frequency", &preset->frequency, 1)) {
            FURI_LOG_E(TAG, "Unable to add Frequency");
            res = SubGhzProtocolStatusErrorParserFrequency;
            break;
        }

        subghz_block_generic_get_preset_name(furi_string_get_cstr(preset->name), temp_str);
        if(!flipper_format_write_string_cstr(
               flipper_format, "Preset", furi_string_get_cstr(temp_str))) {
            FURI_LOG_E(TAG, "Unable to add Preset");
            res = SubGhzProtocolStatusErrorParserPreset;
            break;
        }
        if(!strcmp(furi_string_get_cstr(temp_str), "FuriHalSubGhzPresetCustom")) {
            if(!flipper_format_write_string_cstr(
                   flipper_format, "Custom_preset_module", "CC1101")) {
                FURI_LOG_E(TAG, "Unable to add Custom_preset_module");
                res = SubGhzProtocolStatusErrorParserCustomPreset;
                break;
            }
            if(!flipper_format_write_hex(
                   flipper_format, "Custom_preset_data", preset->data, preset->data_size)) {
                FURI_LOG_E(TAG, "Unable to add Custom_preset_data");
                res = SubGhzProtocolStatusErrorParserCustomPreset;
                break;
            }
        }
        if(!flipper_format_write_string_cstr(flipper_format, "Protocol", instance->protocol_name)) {
            FURI_LOG_E(TAG, "Unable to add Protocol");
            res = SubGhzProtocolStatusErrorParserProtocolName;
            break;
        }
        uint32_t temp = instance->data_count_bit;
        if(!flipper_format_write_uint32(flipper_format, "Bit", &temp, 1)) {
            FURI_LOG_E(TAG, "Unable to add Bit");
            res = SubGhzProtocolStatusErrorParserBitCount;
            break;
        }

        uint8_t key_data[sizeof(uint64_t)] = {0};
        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            key_data[sizeof(uint64_t) - i - 1] = (instance->data >> (i * 8)) & 0xFF;
        }

        if(!flipper_format_write_hex(flipper_format, "Key", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Unable to add Key");
            res = SubGhzProtocolStatusErrorParserKey;
            break;
        }

        if(instance->data_count_bit == 72 &&
           (strcmp(instance->protocol_name, "Nice FloR-S") == 0)) {
            uint32_t temp = (instance->data_2 >> 4) & 0xFFFFF;
            if(!flipper_format_write_uint32(flipper_format, "Data", &temp, 1)) {
                FURI_LOG_E(TAG, "Unable to add Data");
                break;
            }
        }
        res = SubGhzProtocolStatusOk;
    } while(false);
    furi_string_free(temp_str);
    return res;
}

SubGhzProtocolStatus
    subghz_block_generic_deserialize(SubGhzBlockGeneric* instance, FlipperFormat* flipper_format) {
    furi_check(instance);

    SubGhzProtocolStatus res = SubGhzProtocolStatusError;
    FuriString* temp_str;
    temp_str = furi_string_alloc();
    uint32_t temp_data = 0;

    do {
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            res = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        if(!flipper_format_read_uint32(flipper_format, "Bit", (uint32_t*)&temp_data, 1)) {
            FURI_LOG_E(TAG, "Missing Bit");
            res = SubGhzProtocolStatusErrorParserBitCount;
            break;
        }
        instance->data_count_bit = (uint16_t)temp_data;

        uint8_t key_data[sizeof(uint64_t)] = {0};
        if(!flipper_format_read_hex(flipper_format, "Key", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Missing Key");
            res = SubGhzProtocolStatusErrorParserKey;
            break;
        }
        for(uint8_t i = 0; i < sizeof(uint64_t); i++) {
            instance->data = instance->data << 8 | key_data[i];
        }

        res = SubGhzProtocolStatusOk;
    } while(0);

    furi_string_free(temp_str);

    return res;
}

SubGhzProtocolStatus subghz_block_generic_deserialize_check_count_bit(
    SubGhzBlockGeneric* instance,
    FlipperFormat* flipper_format,
    uint16_t count_bit) {
    furi_check(instance);
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize(instance, flipper_format);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }
        if(instance->data_count_bit != count_bit) {
            FURI_LOG_D(TAG, "Wrong number of bits in key");
            ret = SubGhzProtocolStatusErrorValueBitCount;
            break;
        }
    } while(false);
    return ret;
}

void subghz_block_generic_advance_counter(uint32_t* cnt, uint32_t max) {
    furi_assert(cnt);
    uint32_t override_cnt = 0;
    if(subghz_block_generic_global_counter_override_get(&override_cnt)) {
        *cnt = override_cnt;
    } else {
        uint32_t mult = furi_hal_subghz_get_rolling_counter_mult();
        if(mult == 0) mult = 1;
        *cnt += mult;
    }
    if(max) *cnt &= max;
}

void subghz_block_generic_global_publish_cnt(uint32_t cnt, uint8_t length_bit) {
    subghz_block_generic_global.cnt_is_available = true;
    subghz_block_generic_global.cnt_length_bit = length_bit;
    subghz_block_generic_global.current_cnt = cnt;
}

void subghz_block_generic_global_publish_btn(uint8_t btn, uint8_t length_bit) {
    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.btn_length_bit = length_bit;
    subghz_block_generic_global.current_btn = btn;
}

bool subghz_block_read_hex(
    FlipperFormat* flipper_format, const char* key, void* data, size_t size) {
    furi_assert(data);
    if(size == 0 || size > 8) return false;
    uint8_t buf[8] = {0};
    if(!flipper_format_read_hex(flipper_format, key, buf, size)) return false;
    uint64_t value = 0;
    for(size_t i = 0; i < size; i++) value = (value << 8) | buf[i];
    switch(size) {
    case 1:
        *(uint8_t*)data = (uint8_t)value;
        break;
    case 2:
        *(uint16_t*)data = (uint16_t)value;
        break;
    case 4:
        *(uint32_t*)data = (uint32_t)value;
        break;
    case 8:
        *(uint64_t*)data = value;
        break;
    default:
        return false;
    }
    return true;
}

bool subghz_block_write_hex(
    FlipperFormat* flipper_format, const char* key, uint64_t data, size_t size) {
    if(size == 0 || size > 8) return false;
    uint8_t buf[8] = {0};
    for(size_t i = 0; i < size; i++) buf[i] = (uint8_t)(data >> (8 * (size - 1 - i)));
    return flipper_format_write_hex(flipper_format, key, buf, size);
}

void subghz_block_write_disp(
    FlipperFormat* flipper_format, const char* protocol_name, const char* button_name) {
    FuriString* disp = furi_string_alloc_printf("%s - %s", protocol_name, button_name);
    flipper_format_write_string_cstr(flipper_format, "Disp", furi_string_get_cstr(disp));
    furi_string_free(disp);
}
