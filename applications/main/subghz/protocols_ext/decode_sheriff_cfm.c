/* decode_sheriff_cfm.fal : SD get_string for sheriff_cfm (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/sheriff_cfm.h>
#include <lib/subghz/protocols/keeloq_common.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/blocks/generic.h>
#include <string.h>

typedef enum {
    SheriffCfmModelZX750 = 0,
    SheriffCfmModelZX930 = 1,
    SheriffCfmModelCount = 2,
} SheriffCfmModel;

struct SubGhzProtocolDecoderSheriffCfm {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;
    SheriffCfmModel model;
};

static const char* cfm_model_name(SheriffCfmModel model);
static const char* cfm_btn_name(uint8_t btn);
static uint8_t cfm_btn_to_custom(uint8_t btn);
static uint8_t cfm_custom_to_btn(uint8_t custom, uint8_t original_btn);
static uint8_t cfm_get_btn_code(uint8_t original_btn);

static const char* cfm_model_name(SheriffCfmModel model) {
    switch(model) {
    case SheriffCfmModelZX750: return "ZX-750";
    case SheriffCfmModelZX930: return "ZX-930";
    default: return "?";
    }
}

static const char* cfm_btn_name(uint8_t btn) {
    switch(btn) {
    case 0x10: return "Lock";
    case 0x20: return "Unlock";
    case 0x40: return "Trunk";
    case 0x80: return "Panic";
    default: return "?";
    }
}

static uint8_t cfm_btn_to_custom(uint8_t btn) {
    switch(btn) {
    case 0x10: return SUBGHZ_CUSTOM_BTN_UP;
    case 0x20: return SUBGHZ_CUSTOM_BTN_DOWN;
    case 0x40: return SUBGHZ_CUSTOM_BTN_LEFT;
    case 0x80: return SUBGHZ_CUSTOM_BTN_RIGHT;
    default: return SUBGHZ_CUSTOM_BTN_OK;
    }
}

static uint8_t cfm_custom_to_btn(uint8_t custom, uint8_t original_btn) {
    if(custom == SUBGHZ_CUSTOM_BTN_OK) return original_btn;
    if(custom == SUBGHZ_CUSTOM_BTN_UP) return 0x10;
    if(custom == SUBGHZ_CUSTOM_BTN_DOWN) return 0x20;
    if(custom == SUBGHZ_CUSTOM_BTN_LEFT) return 0x40;
    if(custom == SUBGHZ_CUSTOM_BTN_RIGHT) return 0x80;
    return original_btn;
}

static uint8_t cfm_get_btn_code(uint8_t original_btn) {
    return cfm_custom_to_btn(subghz_custom_btn_get(), original_btn);
}

static void sheriff_cfm_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderSheriffCfm* instance = input->decoder;


    subghz_custom_btn_set_original(cfm_btn_to_custom(instance->generic.btn));
    subghz_custom_btn_set_max(4);

    uint8_t selected_btn = cfm_get_btn_code(instance->generic.btn);

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%lX%08lX\r\n"
        "Sn:%08lX Btn:[%s]\r\n"
        "Cnt:%04lX Model:%s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data >> 32),
        (uint32_t)instance->generic.data,
        instance->generic.serial,
        cfm_btn_name(selected_btn),
        (uint32_t)instance->generic.cnt,
        cfm_model_name(instance->model));
}

static const SubGhzDecodeEntry sheriff_cfm_items[] = {{.name = "Sheriff CFM", .get_string = sheriff_cfm_decode}};
static const SubGhzDecodeRegistry sheriff_cfm_registry = {.items = sheriff_cfm_items, .size = 1};
static const FlipperAppPluginDescriptor decode_sheriff_cfm_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &sheriff_cfm_registry};
const FlipperAppPluginDescriptor* decode_sheriff_cfm_ep(void) { return &decode_sheriff_cfm_descriptor; }
