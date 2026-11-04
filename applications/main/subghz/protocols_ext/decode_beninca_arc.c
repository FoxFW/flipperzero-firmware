/* decode_beninca_arc.fal : SD get_string for beninca_arc (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/beninca_arc.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <core/log.h>
#include <stddef.h>
#include <stdint.h>
#include <furi_hal_crypto.h>
#include <lib/subghz/blocks/custom_btn_i.h>

#define BENINCA_ARC_KEY_TYPE 9u

struct SubGhzProtocolDecoderBenincaARC {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;

    SubGhzBlockGeneric generic;

    SubGhzKeystore* keystore;
};

static void get_subghz_protocol_beninca_arc_aes_key(SubGhzKeystore* keystore, uint8_t* aes_key);
static void reverse_bits_in_bytes(uint8_t* data, uint8_t len);
static uint64_t
    subghz_protocol_beninca_arc_decrypt(SubGhzBlockGeneric* generic, SubGhzKeystore* keystore);

static void get_subghz_protocol_beninca_arc_aes_key(SubGhzKeystore* keystore, uint8_t* aes_key) {
    uint64_t mfkey = 0;
    SUBGHZ_KEYSTORE_FOR_EACH(manufacture_code, keystore) {
            if(manufacture_code->type == BENINCA_ARC_KEY_TYPE) {
                mfkey = manufacture_code->key;
                break;
            }
        }

    uint32_t derived_lo = (uint32_t)(mfkey & 0xFFFFFFFF);
    uint32_t derived_hi = (uint32_t)((mfkey >> 32) & 0xFFFFFFFF);

    uint64_t val64_a = ((uint64_t)derived_hi << 32) | derived_lo;
    for(uint8_t i = 0; i < 8; i++) {
        aes_key[i] = (val64_a >> (56 - i * 8)) & 0xFF;
    }

    uint32_t new_lo = ((derived_hi >> 24) & 0xFF) | ((derived_hi >> 8) & 0xFF00) |
                      ((derived_hi << 8) & 0xFF0000) | ((derived_hi << 24) & 0xFF000000);
    uint32_t new_hi = ((derived_lo >> 24) & 0xFF) | ((derived_lo >> 8) & 0xFF00) |
                      ((derived_lo << 8) & 0xFF0000) | ((derived_lo << 24) & 0xFF000000);

    uint64_t val64_b = ((uint64_t)new_hi << 32) | new_lo;
    for(uint8_t i = 0; i < 8; i++) {
        aes_key[i + 8] = (val64_b >> (56 - i * 8)) & 0xFF;
    }
}

static void reverse_bits_in_bytes(uint8_t* data, uint8_t len) {
    for(uint8_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        uint8_t step1 = ((byte & 0x55) << 1) | ((byte >> 1) & 0x55);
        uint8_t step2 = ((step1 & 0x33) << 2) | ((step1 >> 2) & 0x33);
        data[i] = ((step2 & 0x0F) << 4) | (step2 >> 4);
    }
}

static uint64_t
    subghz_protocol_beninca_arc_decrypt(SubGhzBlockGeneric* generic, SubGhzKeystore* keystore) {

    uint8_t encrypted_data[16];

    for(uint8_t i = 0; i < 8; i++) {
        encrypted_data[i] = (generic->data >> (56 - i * 8)) & 0xFF;
        encrypted_data[i + 8] = (generic->data_2 >> (56 - i * 8)) & 0xFF;
    }

    reverse_bits_in_bytes(encrypted_data, 16);

    uint8_t aes_key[16];
    get_subghz_protocol_beninca_arc_aes_key(keystore, aes_key);

    uint8_t decrypted[16];
    furi_hal_crypto_aes128_ecb_decrypt(aes_key, encrypted_data, decrypted);
    memcpy(encrypted_data, decrypted, 16);

    generic->serial = ((uint32_t)encrypted_data[0] << 24) | ((uint32_t)encrypted_data[1] << 16) |
                      ((uint32_t)encrypted_data[2] << 8) | encrypted_data[3];

    generic->btn = encrypted_data[4];

    uint64_t middle_bytes = 0;
    middle_bytes = ((uint64_t)encrypted_data[5] << 32) | ((uint64_t)encrypted_data[6] << 24) |
                   ((uint64_t)encrypted_data[7] << 16) | ((uint64_t)encrypted_data[8] << 8) |
                   encrypted_data[9];

    generic->cnt = ((uint32_t)encrypted_data[10] << 24) | ((uint32_t)encrypted_data[11] << 16) |
                   ((uint32_t)encrypted_data[12] << 8) | encrypted_data[13];

    generic->seed = ((uint16_t)encrypted_data[14] << 8) | encrypted_data[15];

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(generic->btn);
    }
    subghz_custom_btn_set_max(2);

    return middle_bytes;
}

static void beninca_arc_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderBenincaARC* instance = input->decoder;


    uint64_t middle_bytes_dec =
        subghz_protocol_beninca_arc_decrypt(&instance->generic, instance->keystore);

    subghz_block_generic_global.cnt_is_available = true;
    subghz_block_generic_global.cnt_length_bit = 32;
    subghz_block_generic_global.current_cnt = instance->generic.cnt;

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 8;

    furi_string_printf(
        output,
        "%s %db\r\n"
        "Key1:%08llX\r\n"
        "Key2:%08llX\r\n"
        "Sn:%08lX Btn:%02X\r\n"
        "Mc:%0lX Cnt:%0lX\r\n"
        "Fx:%04lX",
        instance->base.protocol->name,
        instance->generic.data_count_bit,
        instance->generic.data,
        instance->generic.data_2,
        instance->generic.serial,
        instance->generic.btn,
        (uint32_t)(middle_bytes_dec & 0xFFFFFFFF),
        instance->generic.cnt,
        instance->generic.seed & 0xFFFF);
}

static const SubGhzDecodeEntry beninca_arc_items[] = {{.name = "Beninca ARC", .get_string = beninca_arc_decode}};
static const SubGhzDecodeRegistry beninca_arc_registry = {.items = beninca_arc_items, .size = 1};
static const FlipperAppPluginDescriptor decode_beninca_arc_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &beninca_arc_registry};
const FlipperAppPluginDescriptor* decode_beninca_arc_ep(void) { return &decode_beninca_arc_descriptor; }
