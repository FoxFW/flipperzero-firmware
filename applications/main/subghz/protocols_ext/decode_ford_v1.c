/* decode_ford_v1.fal : SD get_string for ford_v1 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/ford_v1.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <string.h>

#define FORD_V1_DATA_BYTES   17
#define ford_v1_crc16(data, len) subghz_protocol_blocks_crc16((data), (len), 0x1021, 0x0000)

typedef struct SubGhzProtocolDecoderFordV1 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t crc_calc;

    uint64_t data2;

    uint8_t raw_bytes[FORD_V1_DATA_BYTES + 1];
    uint8_t byte_count;

    ManchesterState manchester_state;
    uint16_t preamble_count;

    uint8_t sync_event_idx;
    uint8_t sync_event_count;
    uint8_t sync_events[8];

    uint8_t encryption_supported;
} SubGhzProtocolDecoderFordV1;

static const char* ford_v1_get_button_name(uint8_t btn);
static void
    ford_v1_raw14_from_internal_keys(uint64_t key1, uint64_t data2, uint8_t raw14_out[14]);

static const char* ford_v1_get_button_name(uint8_t btn) {
    switch(btn) {
    case 0:
        return "Sync";
    case 1:
        return "Lock";
    case 2:
        return "Unlock";
    case 4:
        return "Trunk";
    case 8:
        return "Panic";
    default:
        return "??";
    }
}

static void
    ford_v1_raw14_from_internal_keys(uint64_t key1, uint64_t data2, uint8_t raw14_out[14]) {
    for(int i = 0; i < 7; i++) {
        raw14_out[i] = (uint8_t)((key1 >> (48 - i * 8)) & 0xFFU);
    }
    for(int i = 0; i < 8; i++) {
        raw14_out[7 + i] = (uint8_t)((data2 >> (56 - i * 8)) & 0xFFU);
    }
}

static void ford_v1_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderFordV1* instance = input->decoder;


    uint64_t key1 = instance->generic.data;
    uint64_t key2 = instance->data2;
    uint32_t crc = instance->crc_calc;
    uint16_t crc16 = crc & 0xFFFF;

    if(instance->encryption_supported) {
        const char* btn_name = ford_v1_get_button_name(instance->generic.btn);

        uint16_t calc_crc = crc16;

        bool crc_ok;
        uint8_t raw[FORD_V1_DATA_BYTES];
        ford_v1_raw14_from_internal_keys(key1, key2, raw);
        raw[15] = (uint8_t)((crc >> 8) & 0xFF);
        raw[16] = (uint8_t)(crc & 0xFF);

        uint16_t check_crc = ford_v1_crc16(&raw[3], 12);
        crc_ok = (check_crc == calc_crc);

        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "%014llX%06llX\r\n"
            "%010llX%04lX\r\n"
            "Sn:%08lX Bt:%01X [%s]\r\n"
            "Cnt:%05lX CRC:%04lX [%s]\r\n",
            instance->generic.protocol_name,
            instance->generic.data_count_bit,
            (unsigned long long)key1,
            (unsigned long long)(key2 >> 40),
            (unsigned long long)(key2 & 0xFFFFFFFFFFULL),
            (unsigned long)crc16,
            (unsigned long)instance->generic.serial,
            instance->generic.btn,
            btn_name,
            (unsigned long)instance->generic.cnt,
            (unsigned long)crc16,
            crc_ok ? "OK" : "ERR");
    } else {
        uint8_t raw[FORD_V1_DATA_BYTES];
        ford_v1_raw14_from_internal_keys(key1, key2, raw);
        raw[15] = (uint8_t)((crc >> 8) & 0xFF);
        raw[16] = (uint8_t)(crc & 0xFF);

        uint16_t check_crc = ford_v1_crc16(&raw[3], 12);
        bool crc_ok = (check_crc == crc16);

        uint32_t device_id = ((uint32_t)raw[3] << 24) | ((uint32_t)raw[4] << 16) |
                             ((uint32_t)raw[5] << 8) | raw[6];

        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "%014llX%06llX\r\n"
            "%010llX%04lX\r\n"
            "Sn:%08lX\r\n"
            "CRC:%04lX [%s]\r\n"
            "Encryption not supported !\r\n",
            instance->generic.protocol_name,
            instance->generic.data_count_bit,
            (unsigned long long)key1,
            (unsigned long long)(key2 >> 40),
            (unsigned long long)(key2 & 0xFFFFFFFFFFULL),
            (unsigned long)crc16,
            (unsigned long)device_id,
            (unsigned long)crc16,
            crc_ok ? "OK" : "ERR");
    }
}

static const SubGhzDecodeEntry ford_v1_items[] = {{.name = "Ford V1", .get_string = ford_v1_decode}};
static const SubGhzDecodeRegistry ford_v1_registry = {.items = ford_v1_items, .size = 1};
static const FlipperAppPluginDescriptor decode_ford_v1_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &ford_v1_registry};
const FlipperAppPluginDescriptor* decode_ford_v1_ep(void) { return &decode_ford_v1_descriptor; }
