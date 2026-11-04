/* decode_nero_radio.fal : SD get_string for nero_radio (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/nero_radio.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/encode_plugin.h>

#define NERO_RADIO_DYN_COUNT_BIT   66
#define NERO_RADIO_NORM_COUNT_BIT  72

struct SubGhzProtocolDecoderNeroRadio {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t header_count;
    uint16_t dyn_bit;
    uint16_t dyn_word;
    uint8_t dyn_last;
    bool norm_id_ok;
    uint64_t norm_id;
    uint8_t norm_word;
    uint8_t norm_tail;
};

static void subghz_protocol_nero_radio_parse_data(SubGhzBlockGeneric* instance);

static void subghz_protocol_nero_radio_parse_data(SubGhzBlockGeneric* instance) {
    // Key samples from unit tests
    // 57250501049DD3
    // 57250502049D13
    //
    // Samples from remote
    // 36E4E80104A644
    // 36E4E80204A684
    // 36E4E80304A604
    // 36E4E80404A6E4
    //
    // Held button, one remote (n = hold counter, steps every ~8 frames, restarts per press)
    // F6C18102 n1 6r E5   r = n bit reversed: 0160 1168 2164 316C ... D16B F16F
    // F6C18101 n1 6r 25
    // 57 bit remote, extra bit trails: B2230602 n4 Br 96 + 1, r = 2 for n = 1
    // So r = bit reversed n ^ per remote const (0, A, D seen)

    // possible contents
    // serial  button  hold cnt  ?    check
    // 5725050 1       0         49D  D3
    // 5725050 2       0         49D  13
    // 36E4E80 1       0         4A6  44
    // F6C1810 2       D         16B  E5
    // check depends on serial and button only, not a CRC8 or sum

    // 57 bit frames carry one extra trailing bit
    uint64_t data = instance->data >> (instance->data_count_bit - 56);
    instance->serial = data >> 28;
    instance->btn = (data >> 24) & 0xF;
    instance->cnt = (data >> 20) & 0xF;
}

static void nero_radio_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderNeroRadio* instance = input->decoder;


    uint32_t code_found_hi = instance->generic.data >> 32;
    uint32_t code_found_lo = instance->generic.data & 0x00000000ffffffff;

    if(instance->generic.data_count_bit == NERO_RADIO_DYN_COUNT_BIT) {
        subghz_block_generic_global.btn_is_available = false;
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key:0x%lX%08lX\r\n"
            "Data:%02X\r\n"
            "Rolling long range,\r\nreplay only\r\n",
            instance->generic.protocol_name,
            instance->generic.data_count_bit,
            code_found_hi,
            code_found_lo,
            (uint8_t)instance->generic.data_2);
        return;
    }
    if(instance->generic.data_count_bit == NERO_RADIO_NORM_COUNT_BIT) {
        subghz_block_generic_global.btn_is_available = false;
        uint64_t id = instance->generic.data_2 >> 8;
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key:0x%08lX%08lX\r\n"
            "Id:0x%05lX%08lX Tail:%02X\r\n"
            "Rolling, replay only\r\n",
            instance->generic.protocol_name,
            instance->generic.data_count_bit,
            code_found_hi,
            code_found_lo,
            (uint32_t)(id >> 32),
            (uint32_t)id,
            (uint8_t)instance->generic.data_2);
        return;
    }

    uint64_t code_found_reverse = subghz_protocol_blocks_reverse_key(
        instance->generic.data, instance->generic.data_count_bit);

    uint32_t code_found_reverse_hi = code_found_reverse >> 32;
    uint32_t code_found_reverse_lo = code_found_reverse & 0x00000000ffffffff;

    subghz_protocol_nero_radio_parse_data(&instance->generic);

    // push protocol data to global variable
    subghz_block_generic_global.btn_is_available = false;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 4;
    //

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%lX%08lX\r\n"
        "Yek:0x%lX%08lX\r\n"
        "Sn:0x%07lX Btn:%X\r\n"
        "Hold:%lX Chk:0x%02X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        code_found_hi,
        code_found_lo,
        code_found_reverse_hi,
        code_found_reverse_lo,
        instance->generic.serial,
        instance->generic.btn,
        instance->generic.cnt,
        (uint8_t)(instance->generic.data >> (instance->generic.data_count_bit - 56)));
}

static const SubGhzDecodeEntry nero_radio_items[] = {{.name = "Nero Radio", .get_string = nero_radio_decode}};
static const SubGhzDecodeRegistry nero_radio_registry = {.items = nero_radio_items, .size = 1};
static const FlipperAppPluginDescriptor decode_nero_radio_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &nero_radio_registry};
const FlipperAppPluginDescriptor* decode_nero_radio_ep(void) { return &decode_nero_radio_descriptor; }
