/* decode_secplus_v2.fal : SD get_string for secplus_v2 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/secplus_v2.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>

#define TAG "SubGhzProtocoSecPlusV2"

struct SubGhzProtocolDecoderSecPlus_v2 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_saved_state;
    uint64_t secplus_packet_1;
};

static bool subghz_protocol_secplus_v2_mix_invet(uint8_t invert, uint16_t p[]);
static bool subghz_protocol_secplus_v2_mix_order_decode(uint8_t order, uint16_t p[]);
static bool
    subghz_protocol_secplus_v2_decode_half(uint64_t data, uint8_t roll_array[], uint32_t* fixed);
static void
    subghz_protocol_secplus_v2_remote_controller(SubGhzBlockGeneric* instance, uint64_t packet_1);

static bool subghz_protocol_secplus_v2_mix_invet(uint8_t invert, uint16_t p[]) {

    switch(invert) {
    case 0x00:
        p[0] = ~p[0] & 0x03FF;
        p[1] = ~p[1] & 0x03FF;
        break;
    case 0x01:
        p[1] = ~p[1] & 0x03FF;
        break;
    case 0x02:
        p[2] = ~p[2] & 0x03FF;
        break;
    case 0x04:
        p[0] = ~p[0] & 0x03FF;
        p[1] = ~p[1] & 0x03FF;
        p[2] = ~p[2] & 0x03FF;
        break;
    case 0x05:
    case 0x0a:
        p[0] = ~p[0] & 0x03FF;
        p[2] = ~p[2] & 0x03FF;
        break;
    case 0x06:
        p[1] = ~p[1] & 0x03FF;
        p[2] = ~p[2] & 0x03FF;
        break;
    case 0x08:
        p[0] = ~p[0] & 0x03FF;
        break;
    case 0x09:
        break;
    default:
        FURI_LOG_E(TAG, "Invert FAIL");
        return false;
    }
    return true;
}

static bool subghz_protocol_secplus_v2_mix_order_decode(uint8_t order, uint16_t p[]) {
    uint16_t a = p[0], b = p[1], c = p[2];

    switch(order) {
    case 0x06:
    case 0x09:
        p[2] = a;

        p[0] = c;
        break;
    case 0x08:
    case 0x04:
        p[1] = a;
        p[2] = b;
        p[0] = c;
        break;
    case 0x01:
        p[2] = a;
        p[0] = b;
        p[1] = c;
        break;
    case 0x00:

        p[2] = b;
        p[1] = c;
        break;
    case 0x05:
        p[1] = a;
        p[0] = b;

        break;
    case 0x02:
    case 0x0A:

        break;
    default:
        FURI_LOG_E(TAG, "Order FAIL");
        return false;
    }
    return true;
}

static bool
    subghz_protocol_secplus_v2_decode_half(uint64_t data, uint8_t roll_array[], uint32_t* fixed) {
    uint8_t order = (data >> 34) & 0x0f;
    uint8_t invert = (data >> 30) & 0x0f;
    uint16_t p[3] = {0};

    for(int i = 29; i >= 0; i -= 3) {
        p[0] = p[0] << 1 | bit_read(data, i);
        p[1] = p[1] << 1 | bit_read(data, i - 1);
        p[2] = p[2] << 1 | bit_read(data, i - 2);
    }

    if(!subghz_protocol_secplus_v2_mix_invet(invert, p)) return false;
    if(!subghz_protocol_secplus_v2_mix_order_decode(order, p)) return false;

    data = order << 4 | invert;
    int k = 0;
    for(int i = 6; i >= 0; i -= 2) {
        roll_array[k] = (data >> i) & 0x03;
        if(roll_array[k++] == 3) {
            FURI_LOG_E(TAG, "Roll_Array FAIL");
            return false;
        }
    }

    for(int i = 8; i >= 0; i -= 2) {
        roll_array[k] = (p[2] >> i) & 0x03;
        if(roll_array[k++] == 3) {
            FURI_LOG_E(TAG, "Roll_Array FAIL");
            return false;
        }
    }

    fixed[0] = p[0] << 10 | p[1];
    return true;
}

static void
    subghz_protocol_secplus_v2_remote_controller(SubGhzBlockGeneric* instance, uint64_t packet_1) {
    uint32_t fixed_1[1];
    uint8_t roll_1[9] = {0};
    uint32_t fixed_2[1];
    uint8_t roll_2[9] = {0};
    uint8_t rolling_digits[18] = {0};

    if(subghz_protocol_secplus_v2_decode_half(packet_1, roll_1, fixed_1) &&
       subghz_protocol_secplus_v2_decode_half(instance->data, roll_2, fixed_2)) {
        rolling_digits[0] = roll_2[8];
        rolling_digits[1] = roll_1[8];

        rolling_digits[2] = roll_2[4];
        rolling_digits[3] = roll_2[5];
        rolling_digits[4] = roll_2[6];
        rolling_digits[5] = roll_2[7];

        rolling_digits[6] = roll_1[4];
        rolling_digits[7] = roll_1[5];
        rolling_digits[8] = roll_1[6];
        rolling_digits[9] = roll_1[7];

        rolling_digits[10] = roll_2[0];
        rolling_digits[11] = roll_2[1];
        rolling_digits[12] = roll_2[2];
        rolling_digits[13] = roll_2[3];

        rolling_digits[14] = roll_1[0];
        rolling_digits[15] = roll_1[1];
        rolling_digits[16] = roll_1[2];
        rolling_digits[17] = roll_1[3];

        uint32_t rolling = 0;
        for(int i = 0; i < 18; i++) {
            rolling = (rolling * 3) + rolling_digits[i];
        }

        if(rolling >= 0x10000000) {
            FURI_LOG_E(TAG, "Rolling FAIL");
            instance->cnt = 0;
            instance->btn = 0;
            instance->serial = 0;
        } else {
            instance->cnt = subghz_protocol_blocks_reverse_key(rolling, 28);
            instance->btn = fixed_1[0] >> 12;
            instance->serial = fixed_1[0] << 20 | fixed_2[0];
        }
    } else {
        instance->cnt = 0;
        instance->btn = 0;
        instance->serial = 0;
    }

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->btn);
    }
    subghz_custom_btn_set_max(4);
}

static void secplus_v2_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderSecPlus_v2* instance = input->decoder;

    subghz_protocol_secplus_v2_remote_controller(&instance->generic, instance->secplus_packet_1);

    subghz_block_generic_global.cnt_is_available = true;
    subghz_block_generic_global.cnt_length_bit = 28;
    subghz_block_generic_global.current_cnt = instance->generic.cnt;

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 8;

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "Pk1:0x%lX%08lX\r\n"
        "Pk2:0x%lX%08lX\r\n"
        "Sn:0x%08lX  Btn:0x%01X\r\n"
        "Cnt:%07lX\r\n",

        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->secplus_packet_1 >> 32),
        (uint32_t)instance->secplus_packet_1,
        (uint32_t)(instance->generic.data >> 32),
        (uint32_t)instance->generic.data,
        instance->generic.serial,
        instance->generic.btn,
        instance->generic.cnt);
}

static const SubGhzDecodeEntry secplus_v2_items[] = {{.name = "Security+ 2.0", .get_string = secplus_v2_decode}};
static const SubGhzDecodeRegistry secplus_v2_registry = {.items = secplus_v2_items, .size = 1};
static const FlipperAppPluginDescriptor decode_secplus_v2_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &secplus_v2_registry};
const FlipperAppPluginDescriptor* decode_secplus_v2_ep(void) { return &decode_secplus_v2_descriptor; }
