
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/renault_v1.h>
#include <lib/subghz/protocols/renault_v1_crypto.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>

#define RENAULT_V1_CUSTOM_BTN_MAX  2
#define RENAULT_V1_COUNTER_BITS    16

struct SubGhzProtocolDecoderRenaultV1 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_saved_state;

    uint16_t sync;
    uint16_t te_hi;
    uint16_t te_lo;
};

static const uint8_t renault_v1_btns[] = {0x0, 0x1, 0x2, 0x4, 0x8};

static const uint8_t renault_v1_btn_map[RENAULT_V1_CUSTOM_BTN_MAX][5] = {
    {0x1, 0x1, 0x1, 0x1, 0x1},
    {0x2, 0x2, 0x2, 0x2, 0x2},
};

static void subghz_protocol_renault_v1_check_remote_controller(SubGhzBlockGeneric* instance);
static const char* renault_v1_get_name_button(uint8_t btn);

static void subghz_protocol_renault_v1_check_remote_controller(SubGhzBlockGeneric* instance) {
    instance->serial = (uint32_t)(instance->data >> 32);
    instance->btn = (uint8_t)((instance->data >> 28) & 0x0F);

    subghz_custom_btn_set_max((instance->status == 1) ? RENAULT_V1_CUSTOM_BTN_MAX : 0);

    if(instance->status == 1) {
        const uint32_t iv = instance->seed;
        instance->cnt =
            ((iv >> 28) & 0x0F) | (((iv >> 16) & 0xFF) << 4) | (((iv >> 8) & 0x0F) << 12);
    } else {
        instance->cnt = 0;
    }
}

static const char* renault_v1_get_name_button(uint8_t btn) {
    switch(btn) {
    case 0x0:
        return "Sync";
    case 0x1:
        return "Lock";
    case 0x2:
        return "Unlock";
    case 0x4:
        return "Trunk";
    case 0x8:
        return "Panic";
    default:
        return "??";
    }
}

static void renault_v1_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderRenaultV1* instance = input->decoder;

    subghz_protocol_renault_v1_check_remote_controller(&instance->generic);

    if(instance->generic.status == 1) {
        subghz_block_generic_global_publish_cnt(instance->generic.cnt, RENAULT_V1_COUNTER_BITS);
    }

    subghz_block_generic_global_publish_btn(instance->generic.btn, 4);
    subghz_custom_btn_fill_labels(
        instance->generic.btn, renault_v1_btns, (const uint8_t*)renault_v1_btn_map,
        COUNT_OF(renault_v1_btns),
        (instance->generic.status == 1) ? RENAULT_V1_CUSTOM_BTN_MAX : 0, renault_v1_get_name_button);

    furi_string_cat_printf(
        output,
        "%s\r\n"
        "K1:%016llX\r\n"
        "K2:%06llX Sn:%08lX\r\n"
        "Btn:%02X [%s] %db",
        instance->generic.protocol_name,
        instance->generic.data,
        instance->generic.data_2,
        instance->generic.serial,
        instance->generic.btn,
        renault_v1_get_name_button(instance->generic.btn),
        instance->generic.data_count_bit);

    if(instance->generic.status == 1) {

        furi_string_cat_printf(
            output, "\r\nIV:%08lX Cnt:%04lX", instance->generic.seed, instance->generic.cnt);
    } else if(instance->generic.status == 2) {
        furi_string_cat_str(output, "\r\nBF not found");
    } else {
        furi_string_cat_str(output, "\r\nEncrypted (BF needed)");
    }
}

static const SubGhzDecodeEntry renault_v1_items[] = {{.name = "Hitag-2", .get_string = renault_v1_decode}};
static const SubGhzDecodeRegistry renault_v1_registry = {.items = renault_v1_items, .size = 1};
static const FlipperAppPluginDescriptor decode_renault_v1_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &renault_v1_registry};
const FlipperAppPluginDescriptor* decode_renault_v1_ep(void) { return &decode_renault_v1_descriptor; }
