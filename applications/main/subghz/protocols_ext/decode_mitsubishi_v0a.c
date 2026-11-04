/* decode_mitsubishi_v0a.fal : SD get_string for mitsubishi_v0a (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/mitsubishi_v0a.h>
#include <lib/subghz/blocks/custom_btn_i.h>

struct SubGhzProtocolDecoderMitsubishiV0a {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t header_count;

    uint64_t last_data;
    bool have_last;
};

static uint8_t mitsubishi_v0a_crc8(uint8_t* data, size_t len);
static uint8_t mitsubishi_v0a_calculate_crc(uint64_t data);
static void subghz_protocol_mitsubishi_v0a_check_remote_controller(SubGhzBlockGeneric* instance);
static const char* subghz_protocol_mitsubishi_v0a_get_name_button(uint8_t btn);

static uint8_t mitsubishi_v0a_crc8(uint8_t* data, size_t len) {
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

static uint8_t mitsubishi_v0a_calculate_crc(uint64_t data) {
    uint8_t crc_data[6];
    crc_data[0] = (data >> 48) & 0xFF;
    crc_data[1] = (data >> 40) & 0xFF;
    crc_data[2] = (data >> 32) & 0xFF;
    crc_data[3] = (data >> 24) & 0xFF;
    crc_data[4] = (data >> 16) & 0xFF;
    crc_data[5] = (data >> 8) & 0xFF;

    return mitsubishi_v0a_crc8(crc_data, 6);
}

static void subghz_protocol_mitsubishi_v0a_check_remote_controller(SubGhzBlockGeneric* instance) {

    instance->serial = (uint32_t)((instance->data >> 12) & 0x0FFFFFFF);
    instance->btn = (instance->data >> 8) & 0x0F;
    instance->cnt = (instance->data >> 40) & 0xFFFF;

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->btn);
    }

    subghz_custom_btn_set_max(4);
}

static const char* subghz_protocol_mitsubishi_v0a_get_name_button(uint8_t btn) {
    const char* name_btn[5] = {"Unknown", "Lock", "Unlock", "Trunk", "Horn"};
    return name_btn[btn < 5 ? btn : 0];
}

static void mitsubishi_v0a_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderMitsubishiV0a* instance = input->decoder;


    subghz_protocol_mitsubishi_v0a_check_remote_controller(&instance->generic);
    uint32_t code_found_hi = instance->generic.data >> 32;
    uint32_t code_found_lo = instance->generic.data & 0x00000000ffffffff;

    uint8_t received_crc = instance->generic.data & 0xFF;
    uint8_t calculated_crc = mitsubishi_v0a_calculate_crc(instance->generic.data);
    bool crc_valid = (received_crc == calculated_crc);

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
        instance->generic.btn,
        subghz_protocol_mitsubishi_v0a_get_name_button(instance->generic.btn),
        received_crc,
        crc_valid ? "(OK)" : "(FAIL)");
}

static const SubGhzDecodeEntry mitsubishi_v0a_items[] = {{.name = "Mitsubishi V0-a", .get_string = mitsubishi_v0a_decode}};
static const SubGhzDecodeRegistry mitsubishi_v0a_registry = {.items = mitsubishi_v0a_items, .size = 1};
static const FlipperAppPluginDescriptor decode_mitsubishi_v0a_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &mitsubishi_v0a_registry};
const FlipperAppPluginDescriptor* decode_mitsubishi_v0a_ep(void) { return &decode_mitsubishi_v0a_descriptor; }
