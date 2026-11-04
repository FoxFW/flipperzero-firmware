/* decode_faac_slh.fal : SD get_string for faac_slh (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/faac_slh.h>
#include <lib/subghz/subghz_keystore.h>
#include <m-array.h>
#include <lib/subghz/protocols/keeloq_common.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>

struct SubGhzProtocolDecoderFaacSLH {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    SubGhzKeystore* keystore;
    const char* manufacture_name;

    // Genius mechanism (ported from Unleashed 2026-10-03): optional manufacturer
    // named by the .sub ("Manufacture:" field) so a FAAC-SLH-family signal can be
    // pinned to the right type-5 key instead of last-wins.
    FuriString* manufacture_from_file;
};

static void faac_slh_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderFaacSLH* instance = input->decoder;

    /* KeeLoq/FAAC derivation done internally */
    const bool prog_mode_on = (input->extra_count > 0) && input->extra[0];
    const bool zero_seed_ok = (input->extra_count > 1) && input->extra[1];
    uint32_t code_fix = instance->generic.data >> 32;
    uint32_t code_hop = instance->generic.data & 0xFFFFFFFF;

    // Name the manufacturer when it is not the default one, so a Genius remote does
    // not read as a Faac one. FAAC_SLH keeps the protocol name it has always shown.
    const char* display_name = instance->generic.protocol_name;
    if(instance->manufacture_name && (strcmp(instance->manufacture_name, "FAAC_SLH") != 0)) {
        display_name = instance->manufacture_name;
    }

    if(prog_mode_on) {
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Master Remote Prog Mode\r\n"
            "Ke:%lX%08lX\r\n"
            "Kd:%lX%08lX\r\n"
            "Seed:%08lX mCnt:%02X",
            display_name,
            instance->generic.data_count_bit,
            (uint32_t)(instance->generic.data >> 32),
            (uint32_t)instance->generic.data,
            (uint32_t)(instance->generic.data_2 >> 32),
            (uint32_t)instance->generic.data_2,
            instance->generic.seed,
            (uint8_t)(instance->generic.cnt & 0xFF));
    } else if((!zero_seed_ok) && (instance->generic.seed == 0x0)) {

        subghz_block_generic_global.btn_is_available = true;
        subghz_block_generic_global.current_btn = instance->generic.btn;
        subghz_block_generic_global.btn_length_bit = 4;

        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key:%lX%08lX\r\n"
            "Fix:%08lX\r\n"
            "Hop:%08lX    Btn:%X\r\n"
            "Sn:%07lX Sd:Unknown",
            display_name,
            instance->generic.data_count_bit,
            (uint32_t)(instance->generic.data >> 32),
            (uint32_t)instance->generic.data,
            code_fix,
            code_hop,
            instance->generic.btn,
            instance->generic.serial);
    } else {

        subghz_block_generic_global.cnt_is_available = true;
        subghz_block_generic_global.cnt_length_bit = 20;
        subghz_block_generic_global.current_cnt = instance->generic.cnt;

        subghz_block_generic_global.btn_is_available = true;
        subghz_block_generic_global.current_btn = instance->generic.btn;
        subghz_block_generic_global.btn_length_bit = 4;

        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key:%lX%08lX\r\n"
            "Fix:%08lX    Cnt:%05lX\r\n"
            "Hop:%08lX    Btn:%X\r\n"
            "Sn:%07lX Sd:%08lX",
            display_name,
            instance->generic.data_count_bit,
            (uint32_t)(instance->generic.data >> 32),
            (uint32_t)instance->generic.data,
            code_fix,
            instance->generic.cnt,
            code_hop,
            instance->generic.btn,
            instance->generic.serial,
            instance->generic.seed);
    }
}

static const SubGhzDecodeEntry faac_slh_items[] = {{.name = "Faac SLH", .get_string = faac_slh_decode}};
static const SubGhzDecodeRegistry faac_slh_registry = {.items = faac_slh_items, .size = 1};
static const FlipperAppPluginDescriptor decode_faac_slh_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &faac_slh_registry};
const FlipperAppPluginDescriptor* decode_faac_slh_ep(void) { return &decode_faac_slh_descriptor; }
