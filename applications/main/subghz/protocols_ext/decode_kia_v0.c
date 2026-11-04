/* decode_kia_v0.fal : SD get_string for kia_v0 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/kia_v0.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>

struct SubGhzProtocolDecoderKIA {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t header_count;
};

static uint8_t kia_crc8(uint8_t* data, size_t len);
static uint8_t kia_calculate_crc(uint64_t data);
static void subghz_protocol_kia_check_remote_controller(SubGhzBlockGeneric* instance);
static const char* subghz_protocol_kia_get_name_button(uint8_t btn);

static uint8_t kia_crc8(uint8_t* data, size_t len) {
    uint8_t crc = 0x00;
    for(size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for(size_t j = 0; j < 8; j++) {
            if((crc & 0x80) != 0)
                crc = (uint8_t)((crc << 1) ^ 0x7F);
            else
                crc <<= 1;
        }
    }
    return crc;
}

static uint8_t kia_calculate_crc(uint64_t data) {
    uint8_t crc_data[6];
    crc_data[0] = (data >> 48) & 0xFF;
    crc_data[1] = (data >> 40) & 0xFF;
    crc_data[2] = (data >> 32) & 0xFF;
    crc_data[3] = (data >> 24) & 0xFF;
    crc_data[4] = (data >> 16) & 0xFF;
    crc_data[5] = (data >> 8) & 0xFF;

    return kia_crc8(crc_data, 6);
}

static void subghz_protocol_kia_check_remote_controller(SubGhzBlockGeneric* instance) {

    instance->serial = (uint32_t)((instance->data >> 12) & 0x0FFFFFFF);
    instance->btn = (instance->data >> 8) & 0x0F;
    instance->cnt = (instance->data >> 40) & 0xFFFF;

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->btn);
    }

    subghz_custom_btn_set_max(4);
}

static const char* subghz_protocol_kia_get_name_button(uint8_t btn) {
    const char* name_btn[5] = {"Unknown", "Lock", "Unlock", "Trunk", "Horn"};
    return name_btn[btn < 5 ? btn : 0];
}

static void kia_v0_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderKIA* instance = input->decoder;


    subghz_protocol_kia_check_remote_controller(&instance->generic);
    uint32_t code_found_hi = instance->generic.data >> 32;
    uint32_t code_found_lo = instance->generic.data & 0x00000000ffffffff;

    uint8_t received_crc = instance->generic.data & 0xFF;
    uint8_t calculated_crc = kia_calculate_crc(instance->generic.data);
    bool crc_valid = (received_crc == calculated_crc);

    subghz_custom_btn_set_max(4);
    uint8_t custom_btn_id = subghz_custom_btn_get();
    uint8_t display_btn = (custom_btn_id == SUBGHZ_CUSTOM_BTN_OK) ?
                               instance->generic.btn :
                               (uint8_t)(custom_btn_id & 0x0FU);

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Sn:%07lX  Cnt:%04lX\r\n"
        "Btn:%02X:[%s]\r\n"
        "CRC:%02X %s",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        code_found_hi,
        code_found_lo,
        instance->generic.serial,
        instance->generic.cnt,
        display_btn,
        subghz_protocol_kia_get_name_button(display_btn),
        received_crc,
        crc_valid ? "(OK)" : "(FAIL)");
}

static const SubGhzDecodeEntry kia_v0_items[] = {{.name = "KIA/HYU V0", .get_string = kia_v0_decode}};
static const SubGhzDecodeRegistry kia_v0_registry = {.items = kia_v0_items, .size = 1};
static const FlipperAppPluginDescriptor decode_kia_v0_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &kia_v0_registry};
const FlipperAppPluginDescriptor* decode_kia_v0_ep(void) { return &decode_kia_v0_descriptor; }
