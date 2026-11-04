/* decode_keeloq.fal : SD get_string for keeloq (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/keeloq.h>
#include <lib/subghz/protocols/keeloq_common.h>
#include <lib/subghz/subghz_keystore.h>
#include <m-array.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/subghz_keystore_i.h>

struct SubGhzProtocolDecoderKeeloq {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t header_count;
    SubGhzKeystore* keystore;
    const char* manufacture_name;

    FuriString* manufacture_from_file;
};

static const char* const keeloq_shifted_btn_brands[] = {
    "Nissan",
    "Suzuki",
    "Pandora_SUZUKI",
    "DoorHan",
    "Pandora_DEA",
    "Pandora_GIBIDI",
    "Pandora_MCODE",
    "Pandora_Unknown_1",
    "Pandora_Unknown_2",
    "Alligator_S-275",
    "Pantera_XS/Jaguar",
    "APS-1100_APS-2550",
};

static uint8_t keeloq_btn_get_position(const char* mfname, uint8_t btn_code);

static uint8_t keeloq_btn_get_position(const char* mfname, uint8_t btn_code) {
    for(size_t i = 0; i < COUNT_OF(keeloq_shifted_btn_brands); i++) {
        if(strcmp(mfname, keeloq_shifted_btn_brands[i]) == 0) {
            switch(btn_code) {
            case 0x2: return 1;
            case 0x4: return 2;
            case 0x8: return 3;
            case 0x1: return 4;
            default:  return 0;
            }
        }
    }
    return 0;
}

static void keeloq_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderKeeloq* instance = input->decoder;


    uint32_t hopdecrypt = 0;

    hopdecrypt = (input->extra_count > 0) ? (uint32_t)input->extra[0] : 0;

    uint32_t code_found_hi = instance->generic.data >> 32;
    uint32_t code_found_lo = instance->generic.data & 0x00000000ffffffff;

    uint64_t code_found_reverse = subghz_protocol_blocks_reverse_key(
        instance->generic.data, instance->generic.data_count_bit);
    uint32_t code_found_reverse_hi = code_found_reverse >> 32;
    uint32_t code_found_reverse_lo = code_found_reverse & 0x00000000ffffffff;

    if((strcmp(instance->manufacture_name, "BFT") == 0) ||
       (strcmp(instance->manufacture_name, "Erreka") == 0)) {

        subghz_block_generic_global.cnt_is_available = true;
        subghz_block_generic_global.cnt_length_bit = 16;
        subghz_block_generic_global.current_cnt = instance->generic.cnt;

        subghz_block_generic_global.btn_is_available = true;
        subghz_block_generic_global.current_btn = instance->generic.btn;
        subghz_block_generic_global.btn_length_bit = 4;

        ProgMode prog_mode = (input->extra_count > 1) ? (ProgMode)input->extra[1] : PROG_MODE_OFF;
        if((prog_mode == PROG_MODE_KEELOQ_BFT) || (prog_mode == PROG_MODE_KEELOQ_ERREKA)) {
            furi_string_cat_printf(
                output,
                "%s %dbit\r\n"
                "Key:%08lX%08lX\r\n"
                "Fix:0x%08lX    Cnt:%04lX\r\n"
                "Hop:0x%08lX    Btn:%01X\r\n"
                "MF:%s PRG Sd:%08lX",
                instance->generic.protocol_name,
                instance->generic.data_count_bit,
                code_found_hi,
                code_found_lo,
                code_found_reverse_hi,
                instance->generic.cnt,
                code_found_reverse_lo,
                instance->generic.btn,
                instance->manufacture_name,
                instance->generic.seed);
        } else {
            furi_string_cat_printf(
                output,
                "%s %dbit\r\n"
                "Key:%08lX%08lX\r\n"
                "Fix:0x%08lX    Cnt:%04lX\r\n"
                "Hop:0x%08lX    Btn:%01X\r\n"
                "MF:%s Sd:%08lX",
                instance->generic.protocol_name,
                instance->generic.data_count_bit,
                code_found_hi,
                code_found_lo,
                code_found_reverse_hi,
                instance->generic.cnt,
                hopdecrypt,
                instance->generic.btn,
                instance->manufacture_name,
                instance->generic.seed);
        }
    } else if(strcmp(instance->manufacture_name, "Unknown") == 0) {
        subghz_block_generic_global.btn_is_available = true;
        subghz_block_generic_global.current_btn = instance->generic.btn;
        subghz_block_generic_global.btn_length_bit = 4;
        instance->generic.cnt = 0x0;
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key:%08lX%08lX\r\n"
            "Fix:0x%08lX    Cnt:???\r\n"
            "Hop:0x%08lX    Btn:%01X\r\n"
            "MF:%s",
            instance->generic.protocol_name,
            instance->generic.data_count_bit,
            code_found_hi,
            code_found_lo,
            code_found_reverse_hi,
            code_found_reverse_lo,
            instance->generic.btn,
            instance->manufacture_name);
    } else {
        subghz_block_generic_global.cnt_is_available = true;
        subghz_block_generic_global.cnt_length_bit = 16;
        subghz_block_generic_global.current_cnt = instance->generic.cnt;
        subghz_block_generic_global.btn_is_available = true;
        subghz_block_generic_global.current_btn = instance->generic.btn;
        subghz_block_generic_global.btn_length_bit = 4;
        uint8_t btn_pos =
            keeloq_btn_get_position(instance->manufacture_name, instance->generic.btn);
        if(btn_pos > 0) {
            furi_string_cat_printf(
                output,
                "%s %dbit\r\n"
                "Key:%08lX%08lX\r\n"
                "Fix:0x%08lX    Cnt:%04lX\r\n"
                "Hop:0x%08lX  Btn:%lX(B%lu)\r\n"
                "MF:%s",
                instance->generic.protocol_name,
                instance->generic.data_count_bit,
                code_found_hi,
                code_found_lo,
                code_found_reverse_hi,
                instance->generic.cnt,
                hopdecrypt,
                (uint32_t)instance->generic.btn,
                (uint32_t)btn_pos,
                instance->manufacture_name);
        } else {
            furi_string_cat_printf(
                output,
                "%s %dbit\r\n"
                "Key:%08lX%08lX\r\n"
                "Fix:0x%08lX    Cnt:%04lX\r\n"
                "Hop:0x%08lX    Btn:%01X\r\n"
                "MF:%s",
                instance->generic.protocol_name,
                instance->generic.data_count_bit,
                code_found_hi,
                code_found_lo,
                code_found_reverse_hi,
                instance->generic.cnt,
                hopdecrypt,
                instance->generic.btn,
                instance->manufacture_name);
        }
    }
}

static const SubGhzDecodeEntry keeloq_items[] = {{.name = "KeeLoq", .get_string = keeloq_decode}};
static const SubGhzDecodeRegistry keeloq_registry = {.items = keeloq_items, .size = 1};
static const FlipperAppPluginDescriptor decode_keeloq_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &keeloq_registry};
const FlipperAppPluginDescriptor* decode_keeloq_ep(void) { return &decode_keeloq_descriptor; }
