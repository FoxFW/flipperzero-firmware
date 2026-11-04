/* decode_scher_khan.fal : SD get_string for scher_khan (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/scher_khan.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/blocks/generic.h>
#include <string.h>

#define TAG "SubGhzProtocolScherKhan"

struct SubGhzProtocolDecoderScherKhan {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t header_count;
    const char* protocol_name;
};

static const uint8_t sk_pi_bytes[146] = {
    0x24, 0x3F, 0x6A, 0x88, 0x85, 0xA3, 0x08, 0xD3,
    0x13, 0x19, 0x8A, 0x2E, 0x03, 0x70, 0x73, 0x44,
    0xA4, 0x09, 0x38, 0x22, 0x29, 0x9F, 0x31, 0xD0,
    0x08, 0x2E, 0xFA, 0x98, 0xEC, 0x4E, 0x6C, 0x89,
    0x45, 0x28, 0x21, 0xE6, 0x38, 0xD0, 0x13, 0x77,
    0xBE, 0x54, 0x66, 0xCF, 0x34, 0xE9, 0x0C, 0x6C,
    0xC0, 0xAC, 0x29, 0xB7, 0xC9, 0x7C, 0x50, 0xDD,
    0x3F, 0x84, 0xD5, 0xB5, 0xB5, 0x47, 0x09, 0x17,
    0x92, 0x16, 0xD5, 0xD9, 0x89, 0x79, 0xFB, 0x1B,
    0xD1, 0x31, 0x0B, 0xA6, 0x98, 0xDF, 0xB5, 0xAC,
    0x2F, 0xFD, 0x72, 0xDB, 0xD0, 0x1A, 0xDF, 0xB7,
    0xB8, 0xE1, 0xAF, 0xED, 0x6A, 0x26, 0x7E, 0x96,
    0xBA, 0x7C, 0x90, 0x45, 0xF1, 0x2C, 0x7F, 0x99,
    0x24, 0xA1, 0x99, 0x47, 0xB3, 0x91, 0x6C, 0xF7,
    0x08, 0x01, 0xF2, 0xE2, 0x85, 0x8E, 0xFC, 0x16,
    0x63, 0x69, 0x20, 0xD8, 0x71, 0x57, 0x4E, 0x69,
    0xA4, 0x58, 0xFE, 0xA3, 0xF4, 0x93, 0x3D, 0x7E,
    0x0D, 0x95, 0x74, 0x8F, 0x72, 0x8E, 0xB6, 0x58,
    0x71, 0x8B,
};

static const uint8_t sk_pro1_encoded[64] = {
    0x63, 0x5A, 0x58, 0x98, 0xD6, 0xB3, 0x7E, 0x91,
    0x37, 0x1E, 0xBB, 0x78, 0x43, 0x06, 0x20, 0x65,
    0xC7, 0x4B, 0x3F, 0x73, 0x0D, 0xAE, 0x36, 0x86,
    0x1E, 0x7A, 0xDA, 0xEB, 0xBB, 0x0F, 0x5A, 0xA9,
    0x26, 0x3A, 0x51, 0xB2, 0x7A, 0xE0, 0x44, 0x61,
    0xAC, 0x53, 0x02, 0xFA, 0x42, 0xBD, 0x2D, 0x6F,
    0x85, 0xBB, 0x4A, 0xB5, 0xF9, 0x28, 0x46, 0xAF,
    0x2B, 0xB4, 0xB2, 0x90, 0xD0, 0x05, 0x19, 0x64,
};

static const uint8_t sk_pro2_encoded[64] = {
    0x90, 0x27, 0xA3, 0x9C, 0xEA, 0x6B, 0x8B, 0x4F,
    0x91, 0x47, 0x58, 0x87, 0x8A, 0xD8, 0xD1, 0x99,
    0x3B, 0xCD, 0x15, 0xFE, 0xF4, 0x1D, 0xEE, 0xE1,
    0xBF, 0xA4, 0x9E, 0xCB, 0x1C, 0x72, 0x5F, 0x95,
    0xAC, 0x28, 0xB0, 0x36, 0xE3, 0x2B, 0x1B, 0xAC,
    0x30, 0x91, 0xFE, 0x62, 0xD0, 0xD3, 0x6B, 0xA6,
    0x4F, 0x64, 0xC0, 0xF2, 0xD2, 0xCF, 0xCA, 0x36,
    0x53, 0x3D, 0x36, 0xAA, 0x33, 0x67, 0x19, 0x7F,
};

static const uint8_t sk_pro_crc_encoded[2] = {
    0x5B, 0x8C,
};

static void sk_pi_decode(uint8_t* out, const uint8_t* encoded, size_t offset, size_t len);
static uint8_t scher_khan_pro_checksum(const uint8_t* data, uint8_t poly);
static void scher_khan_pro_decrypt(
    const uint8_t* encrypted,
    uint8_t* decrypted,
    const uint8_t* key_table);
static const char* scher_khan_btn_name(uint8_t btn);
static uint8_t scher_khan_btn_to_custom(uint8_t btn);
static uint8_t scher_khan_custom_to_btn(uint8_t custom, uint8_t original_btn);
static uint8_t scher_khan_get_btn_code(uint8_t original_btn);
static void subghz_protocol_scher_khan_check_remote_controller(
    SubGhzBlockGeneric* instance,
    const char** protocol_name);

static void sk_pi_decode(uint8_t* out, const uint8_t* encoded, size_t offset, size_t len) {
    for(size_t i = 0; i < len; i++) {
        out[i] = encoded[i] ^ sk_pi_bytes[offset + i];
    }
}

static uint8_t scher_khan_pro_checksum(const uint8_t* data, uint8_t poly) {
    uint8_t cs = 0xFF;
    for(uint8_t a = 0; a < 6; a++) {
        uint8_t c = data[a];
        for(uint8_t b = 0; b < 8; b++) {
            uint8_t _cs = cs;
            cs <<= 1;
            if((_cs ^ c) & 0x80) cs ^= poly;
            c <<= 1;
        }
    }
    return cs;
}

static void scher_khan_pro_decrypt(
    const uint8_t* encrypted,
    uint8_t* decrypted,
    const uint8_t* key_table) {
    memset(decrypted, 0, 7);
    decrypted[0] = encrypted[0];
    uint8_t pkt_cnt = encrypted[0] & 0x0F;

    for(int i = 1; i < 6; i++) {
        for(int j = 0; j < 4; j++) {
            uint8_t cr = key_table[(pkt_cnt << 2) + j];
            uint8_t s = encrypted[i];

            uint8_t bit = 0x80 >> (cr >> 4);
            if((s & bit) == bit) decrypted[i] |= 0x80 >> (j << 1);

            bit = 0x80 >> (cr & 0x0F);
            if((s & bit) == bit) decrypted[i] |= 0x80 >> ((j << 1) + 1);
        }
        pkt_cnt = (pkt_cnt + 1) & 0x0F;
        decrypted[i] ^= encrypted[i - 1];
    }
}

static const char* scher_khan_btn_name(uint8_t btn) {
    switch(btn) {
    case 0x1: return "Lock";
    case 0x2: return "Unlock";
    case 0x3: return "Lock+Unlock";
    case 0x4: return "Trunk";
    case 0x5: return "Lock+Trunk";
    case 0x6: return "Unlock+Trunk";
    case 0x7: return "Lk+Ul+Tr";
    case 0x8: return "Start";
    case 0x9: return "Lock+Start";
    case 0xA: return "Unlock+Start";
    case 0xC: return "Trunk+Start";
    case 0xF: return "All/Panic";
    default:  return "?";
    }
}

static uint8_t scher_khan_btn_to_custom(uint8_t btn) {
    switch(btn) {
    case 0x1: return SUBGHZ_CUSTOM_BTN_UP;
    case 0x2: return SUBGHZ_CUSTOM_BTN_DOWN;
    case 0x4: return SUBGHZ_CUSTOM_BTN_LEFT;
    case 0x8: return SUBGHZ_CUSTOM_BTN_RIGHT;
    default:  return SUBGHZ_CUSTOM_BTN_OK;
    }
}

static uint8_t scher_khan_custom_to_btn(uint8_t custom, uint8_t original_btn) {
    if(custom == SUBGHZ_CUSTOM_BTN_OK) return original_btn;

    uint8_t page = subghz_custom_btn_get_page();

    FURI_LOG_I(TAG, "custom_to_btn: original=0x%02X custom=%u page=%u",
        original_btn, custom, page);

    if(page == 0) {
        switch(custom) {
        case SUBGHZ_CUSTOM_BTN_UP:    return 0x1;
        case SUBGHZ_CUSTOM_BTN_DOWN:  return 0x2;
        case SUBGHZ_CUSTOM_BTN_LEFT:  return 0x4;
        case SUBGHZ_CUSTOM_BTN_RIGHT: return 0x8;
        }
    } else if(page == 1) {
        switch(custom) {
        case SUBGHZ_CUSTOM_BTN_UP:    return 0x3;
        case SUBGHZ_CUSTOM_BTN_DOWN:  return 0x5;
        case SUBGHZ_CUSTOM_BTN_LEFT:  return 0x6;
        case SUBGHZ_CUSTOM_BTN_RIGHT: return 0x7;
        }
    } else {
        switch(custom) {
        case SUBGHZ_CUSTOM_BTN_UP:    return 0x9;
        case SUBGHZ_CUSTOM_BTN_DOWN:  return 0xA;
        case SUBGHZ_CUSTOM_BTN_LEFT:  return 0xC;
        case SUBGHZ_CUSTOM_BTN_RIGHT: return 0xF;
        }
    }
    return original_btn;
}

static uint8_t scher_khan_get_btn_code(uint8_t original_btn) {
    return scher_khan_custom_to_btn(subghz_custom_btn_get(), original_btn);
}

static void subghz_protocol_scher_khan_check_remote_controller(
    SubGhzBlockGeneric* instance,
    const char** protocol_name) {

    switch(instance->data_count_bit) {
    case 35:
        *protocol_name = "MAGIC CODE, Static";
        instance->serial = 0;
        instance->btn = 0;
        instance->cnt = 0;
        break;
    case 51:
        *protocol_name = "MAGIC CODE, Dynamic";
        instance->serial = ((instance->data >> 24) & 0xFFFFFF0) | ((instance->data >> 20) & 0x0F);
        instance->btn = (instance->data >> 24) & 0x0F;
        instance->cnt = instance->data & 0xFFFF;
        break;
    case 57: {
        uint8_t kb[7];
        for(int i = 0; i < 7; i++) {
            kb[i] = (uint8_t)((instance->data >> (48 - i * 8)) & 0xFF);
        }

        uint8_t crc_polys[2];
        sk_pi_decode(crc_polys, sk_pro_crc_encoded, 144, 2);

        uint8_t key_table[64];
        uint8_t decrypted[7];
        bool found = false;

        uint8_t computed_crc = scher_khan_pro_checksum(kb, crc_polys[0]);
        if(computed_crc == kb[6]) {
            sk_pi_decode(key_table, sk_pro1_encoded, 0, 64);
            scher_khan_pro_decrypt(kb, decrypted, key_table);
            *protocol_name = "MAGIC CODE PRO1";
            found = true;
        }

        if(!found) {
            computed_crc = scher_khan_pro_checksum(kb, crc_polys[1]);
            if(computed_crc == kb[6]) {
                sk_pi_decode(key_table, sk_pro2_encoded, 64, 64);
                scher_khan_pro_decrypt(kb, decrypted, key_table);
                *protocol_name = "MAGIC CODE PRO2";
                found = true;
            }
        }

        if(found) {
            instance->serial =
                ((uint32_t)decrypted[0] << 16) |
                ((uint32_t)decrypted[1] << 8) |
                (uint32_t)decrypted[2];
            instance->btn = decrypted[3] & 0x0F;
            instance->cnt =
                ((uint16_t)decrypted[4] << 8) | decrypted[5];
        } else {
            *protocol_name = "MAGIC CODE PRO/PRO2";
            instance->serial = 0;
            instance->btn = 0;
            instance->cnt = 0;
        }
        break;
    }
    case 63:
        *protocol_name = "MAGIC CODE, Response";
        instance->serial = 0;
        instance->btn = 0;
        instance->cnt = 0;
        break;
    case 64:
        *protocol_name = "MAGICAR, Response";
        instance->serial = 0;
        instance->btn = 0;
        instance->cnt = 0;
        break;
    case 81:
    case 82:
        *protocol_name = "MAGIC CODE PRO,\n Response";
        instance->serial = 0;
        instance->btn = 0;
        instance->cnt = 0;
        break;
    default:
        *protocol_name = "Unknown";
        instance->serial = 0;
        instance->btn = 0;
        instance->cnt = 0;
        break;
    }
}

static void scher_khan_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderScherKhan* instance = input->decoder;


    subghz_protocol_scher_khan_check_remote_controller(
        &instance->generic, &instance->protocol_name);

    subghz_custom_btn_set_original(scher_khan_btn_to_custom(instance->generic.btn));
    subghz_custom_btn_set_max(4);
    subghz_custom_btn_set_pages(true);
        subghz_custom_btn_set_max_pages(3);

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%lX%08lX\r\n"
        "Sn:%07lX Btn:[%s]\r\n"
        "Cntr:%04lX\r\n"
        "Pt: %s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data >> 32),
        (uint32_t)instance->generic.data,
        instance->generic.serial,
        scher_khan_btn_name(scher_khan_get_btn_code(instance->generic.btn)),
        instance->generic.cnt,
        instance->protocol_name);
}

static const SubGhzDecodeEntry scher_khan_items[] = {{.name = "Scher-Khan", .get_string = scher_khan_decode}};
static const SubGhzDecodeRegistry scher_khan_registry = {.items = scher_khan_items, .size = 1};
static const FlipperAppPluginDescriptor decode_scher_khan_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &scher_khan_registry};
const FlipperAppPluginDescriptor* decode_scher_khan_ep(void) { return &decode_scher_khan_descriptor; }
