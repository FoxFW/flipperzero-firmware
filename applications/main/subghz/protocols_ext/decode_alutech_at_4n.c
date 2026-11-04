/* decode_alutech_at_4n.fal : SD get_string for alutech_at_4n (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/alutech_at_4n.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>

#define SUBGHZ_NO_ALUTECH_AT_4N_RAINBOW_TABLE         0xFFFFFFFFFFFFFFFF
#define SUBGHZ_ALUTECH_AT_4N_RAINBOW_TABLE_SIZE_BYTES 32

struct SubGhzProtocolDecoderAlutech_at_4n {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t crc;
    uint16_t header_count;

    const char* alutech_at_4n_rainbow_table_file_name;
};

static uint32_t subghz_protocol_alutech_at_4n_get_magic_data_from_buffer(
    uint8_t* buffer,
    uint8_t number_alutech_at_4n_magic_data);
static uint8_t subghz_protocol_alutech_at_4n_crc(uint64_t data);
static uint8_t subghz_protocol_alutech_at_4n_decrypt_data_crc(uint8_t data);
static uint64_t subghz_protocol_alutech_at_4n_decrypt(uint64_t data, const char* file_name);
static void subghz_protocol_alutech_at_4n_remote_controller(
    SubGhzBlockGeneric* instance,
    uint8_t crc,
    const char* file_name);

static uint32_t subghz_protocol_alutech_at_4n_get_magic_data_from_buffer(
    uint8_t* buffer,
    uint8_t number_alutech_at_4n_magic_data) {
    uint32_t address = number_alutech_at_4n_magic_data * sizeof(uint32_t);
    uint32_t alutech_at_4n_magic_data = 0;

    for(size_t i = address; i < (address + sizeof(uint32_t)); i++) {
        alutech_at_4n_magic_data = (alutech_at_4n_magic_data << 8) | buffer[i];
    }

    return alutech_at_4n_magic_data;
}

static uint8_t subghz_protocol_alutech_at_4n_crc(uint64_t data) {
    uint8_t* p = (uint8_t*)&data;
    uint8_t crc = 0xff;
    for(uint8_t y = 0; y < 8; y++) {
        crc = crc ^ p[y];
        for(uint8_t i = 0; i < 8; i++) {
            if((crc & 0x80) != 0) {
                crc <<= 1;
                crc ^= 0x31;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static uint8_t subghz_protocol_alutech_at_4n_decrypt_data_crc(uint8_t data) {
    uint8_t crc = data;
    for(uint8_t i = 0; i < 8; i++) {
        if((crc & 0x80) != 0) {
            crc <<= 1;
            crc ^= 0x31;
        } else {
            crc <<= 1;
        }
    }
    return ~crc;
}

static uint64_t subghz_protocol_alutech_at_4n_decrypt(uint64_t data, const char* file_name) {

    if(!file_name) return SUBGHZ_NO_ALUTECH_AT_4N_RAINBOW_TABLE;

    uint8_t buffer[SUBGHZ_ALUTECH_AT_4N_RAINBOW_TABLE_SIZE_BYTES] = {0};
    uint8_t* buffer_ptr = (uint8_t*)&buffer;

    if(subghz_keystore_raw_get_data(
           file_name, 0, buffer, SUBGHZ_ALUTECH_AT_4N_RAINBOW_TABLE_SIZE_BYTES)) {
    } else {
        return SUBGHZ_NO_ALUTECH_AT_4N_RAINBOW_TABLE;
    }

    uint8_t* p = (uint8_t*)&data;
    uint32_t data1 = p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
    uint32_t data2 = p[4] << 24 | p[5] << 16 | p[6] << 8 | p[7];
    uint32_t data3 = 0;
    uint32_t magic_data[] = {
        subghz_protocol_alutech_at_4n_get_magic_data_from_buffer(buffer_ptr, 0),
        subghz_protocol_alutech_at_4n_get_magic_data_from_buffer(buffer_ptr, 1),
        subghz_protocol_alutech_at_4n_get_magic_data_from_buffer(buffer_ptr, 2),
        subghz_protocol_alutech_at_4n_get_magic_data_from_buffer(buffer_ptr, 3),
        subghz_protocol_alutech_at_4n_get_magic_data_from_buffer(buffer_ptr, 4),
        subghz_protocol_alutech_at_4n_get_magic_data_from_buffer(buffer_ptr, 5)};

    uint32_t i = magic_data[0];
    do {
        data2 = data2 -
                ((magic_data[1] + (data1 << 4)) ^ ((magic_data[2] + (data1 >> 5)) ^ (data1 + i)));
        data3 = data2 + i;
        i += magic_data[3];
        data1 =
            data1 - ((magic_data[4] + (data2 << 4)) ^ ((magic_data[5] + (data2 >> 5)) ^ data3));
    } while(i != 0);

    p[0] = (uint8_t)(data1 >> 24);
    p[1] = (uint8_t)(data1 >> 16);
    p[3] = (uint8_t)data1;
    p[4] = (uint8_t)(data2 >> 24);
    p[5] = (uint8_t)(data2 >> 16);
    p[2] = (uint8_t)(data1 >> 8);
    p[6] = (uint8_t)(data2 >> 8);
    p[7] = (uint8_t)data2;

    return data;
}

static void subghz_protocol_alutech_at_4n_remote_controller(
    SubGhzBlockGeneric* instance,
    uint8_t crc,
    const char* file_name) {

    bool status = false;
    uint64_t data = subghz_protocol_blocks_reverse_key(instance->data, 64);
    crc = subghz_protocol_blocks_reverse_key(crc, 8);

    if(crc == subghz_protocol_alutech_at_4n_crc(data)) {
        data = subghz_protocol_alutech_at_4n_decrypt(data, file_name);
        status = true;
    }

    if(status && ((uint8_t)(data >> 56) ==
                  subghz_protocol_alutech_at_4n_decrypt_data_crc((uint8_t)((data >> 8) & 0xFF)))) {
        instance->btn = (uint8_t)data & 0xFF;
        instance->cnt = (uint16_t)(data >> 8) & 0xFFFF;
        instance->serial = (uint32_t)(data >> 24) & 0xFFFFFFFF;
    }

    if(!status) {
        instance->btn = 0;
        instance->cnt = 0;
        instance->serial = 0;
    }

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->btn);
    }
    subghz_custom_btn_set_max(4);
}

static void alutech_at_4n_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderAlutech_at_4n* instance = input->decoder;

    subghz_protocol_alutech_at_4n_remote_controller(
        &instance->generic, instance->crc, instance->alutech_at_4n_rainbow_table_file_name);
    uint32_t code_found_hi = instance->generic.data >> 32;
    uint32_t code_found_lo = instance->generic.data & 0x00000000ffffffff;

    subghz_block_generic_global.cnt_is_available = true;
    subghz_block_generic_global.cnt_length_bit = 16;
    subghz_block_generic_global.current_cnt = instance->generic.cnt;

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 8;

    furi_string_cat_printf(
        output,
        "%s\r\n"
        "Key:0x%08lX%08lX\nCRC:%02X  %dbit\r\n"
        "Sn:0x%08lX  Btn:0x%01X\r\n"
        "Cnt:%04lX\r\n",
        instance->generic.protocol_name,
        code_found_hi,
        code_found_lo,
        (uint8_t)instance->crc,
        instance->generic.data_count_bit,
        instance->generic.serial,
        instance->generic.btn,
        instance->generic.cnt);
}

static const SubGhzDecodeEntry alutech_at_4n_items[] = {{.name = "Alutech AT-4N", .get_string = alutech_at_4n_decode}};
static const SubGhzDecodeRegistry alutech_at_4n_registry = {.items = alutech_at_4n_items, .size = 1};
static const FlipperAppPluginDescriptor decode_alutech_at_4n_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &alutech_at_4n_registry};
const FlipperAppPluginDescriptor* decode_alutech_at_4n_ep(void) { return &decode_alutech_at_4n_descriptor; }
