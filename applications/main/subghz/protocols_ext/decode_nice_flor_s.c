/* decode_nice_flor_s.fal : SD get_string for nice_flor_s (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/nice_flor_s.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/nice_flor_s_o.h>

#define NICE_ONE_NAME                               "Nice One"
#define SUBGHZ_NICE_FLOR_S_RAINBOW_TABLE_SIZE_BYTES 32
#define SUBGHZ_NO_NICE_FLOR_S_RAINBOW_TABLE         0

struct SubGhzProtocolDecoderNiceFlorS {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    const char* nice_flor_s_rainbow_table_file_name;

    uint16_t ic;
    uint8_t held_count_bit;
    bool o_code : 1;
    bool o_code_no_ic : 1;
    bool skip_o_code : 1;
    bool held_repeat : 1;
    bool has_held : 1;
    uint8_t fresh_run : 2;
    uint8_t cert_cnt;
    uint32_t cert;
    uint32_t held_lo;
    uint32_t held_hi;
    uint32_t held_trail;
};

static void subghz_protocol_nice_flor_s_remote_controller(
    SubGhzBlockGeneric* instance,
    const char* file_name,
    uint16_t ic,
    bool o_code) {
    if(!file_name) {
        instance->cnt = 0;
        instance->serial = 0;
        instance->btn = 0;
    } else {
        uint8_t buf[SUBGHZ_NICE_FLOR_S_RAINBOW_TABLE_SIZE_BYTES] = {0};
        if(!subghz_keystore_raw_get_data(
               file_name, 0, buf, SUBGHZ_NICE_FLOR_S_RAINBOW_TABLE_SIZE_BYTES)) {
            instance->cnt = 0;
            instance->serial = 0;
            instance->btn = 0;
        } else if(o_code) {
            uint32_t serial = 0;
            uint16_t cnt = 0;
            uint8_t btn = 0;
            nice_o_decode_frame(instance->data, ic, buf, &serial, &cnt, &btn);
            instance->serial = serial;
            instance->cnt = cnt;
            instance->btn = btn;
        } else {
            uint64_t decrypt = nice_o_decrypt_buf(instance->data, NICE_FLOR_S_IC_PLAIN, buf);
            instance->cnt = decrypt & 0xFFFF;
            instance->serial = (decrypt >> 16) & 0xFFFFFFF;
            instance->btn = (decrypt >> 48) & 0xF;
        }
    }

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->btn);
    }
    subghz_custom_btn_set_max(4);
}

static uint32_t subghz_protocol_nice_flor_s_cert(SubGhzBlockGeneric* generic) {
    return (uint32_t)((generic->data_2 >> 8) ^ generic->data ^ generic->serial) & 0xFFFFFF;
}

static void nice_flor_s_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderNiceFlorS* instance = input->decoder;

    subghz_protocol_nice_flor_s_remote_controller(
        &instance->generic,
        instance->nice_flor_s_rainbow_table_file_name,
        instance->ic,
        instance->o_code);

    subghz_block_generic_global.cnt_is_available = true;
    subghz_block_generic_global.cnt_length_bit = 16;
    subghz_block_generic_global.current_cnt = instance->generic.cnt;

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 4;

    if(instance->o_code_no_ic) {
        subghz_block_generic_global.cnt_is_available = false;
        subghz_block_generic_global.btn_is_available = false;
        furi_string_cat_printf(
            output,
            "%s %db\r\n"
            "Key:%013llX\r\n"
            "No installer code\r\n"
            "Use Nice O-Code app\r\n"
            "to receive and decode",
            NICE_O_NAME,
            instance->generic.data_count_bit,
            instance->generic.data);
    } else if(instance->o_code) {
        furi_string_cat_printf(
            output, "%s %db\r\n", NICE_O_NAME, instance->generic.data_count_bit);
        if(instance->generic.data_count_bit == NICE_ONE_COUNT_BIT) {
            furi_string_cat_printf(
                output, "Key:%013llX%llX\r\n", instance->generic.data, instance->generic.data_2);
        } else {
            furi_string_cat_printf(output, "Key:%013llX\r\n", instance->generic.data);
        }
        furi_string_cat_printf(
            output,
            "Sn:%05lX IC:%04X\r\n"
            "Cnt:%04lX Btn:%02X\r\n",
            instance->generic.serial,
            instance->ic,
            instance->generic.cnt,
            instance->generic.btn);
        if(instance->generic.data_count_bit == NICE_O_LONG_COUNT_BIT) {
            furi_string_cat_printf(
                output, "Cert:%06lX\r\n", subghz_protocol_nice_flor_s_cert(&instance->generic));
        } else if(instance->cert && (instance->cert_cnt < NICE_O_CERT_SENDS)) {
            furi_string_cat_printf(
                output,
                "Cert:%06lX %u left\r\n",
                instance->cert,
                NICE_O_CERT_SENDS - instance->cert_cnt);
        }
    } else if(instance->generic.data_count_bit == NICE_ONE_COUNT_BIT) {
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key:%013llX%llX\r\n"
            "Sn:%05lX\r\n"
            "Cnt:%04lX Btn:%02X\r\n",
            NICE_ONE_NAME,
            instance->generic.data_count_bit,
            instance->generic.data,
            instance->generic.data_2,
            instance->generic.serial,
            instance->generic.cnt,
            instance->generic.btn);
    } else {
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key:0x%013llX\r\n"
            "Sn:%05lX\r\n"
            "Cnt:%04lX Btn:%02X\r\n",
            instance->generic.protocol_name,
            instance->generic.data_count_bit,
            instance->generic.data,
            instance->generic.serial,
            instance->generic.cnt,
            instance->generic.btn);
    }
}


static const SubGhzDecodeEntry nice_flor_s_items[] = {{.name = "Nice FloR-S", .get_string = nice_flor_s_decode}};
static const SubGhzDecodeRegistry nice_flor_s_registry = {.items = nice_flor_s_items, .size = 1};
static const FlipperAppPluginDescriptor decode_nice_flor_s_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &nice_flor_s_registry};
const FlipperAppPluginDescriptor* decode_nice_flor_s_ep(void) { return &decode_nice_flor_s_descriptor; }
