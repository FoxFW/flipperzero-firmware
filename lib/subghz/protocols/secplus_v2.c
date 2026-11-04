#include "secplus_v2.h"

#include "common.h"
#include "../encode_plugin.h"
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#include "../blocks/custom_btn_i.h"

#define TAG "SubGhzProtocoSecPlusV2"

static bool secplus_v2_raw_replay = false; // #16: TxRaw verbatim replay (freeze counter)

#define SECPLUS_V2_HEADER      0x3C0000000000
#define SECPLUS_V2_HEADER_MASK 0xFFFF3C0000000000
#define SECPLUS_V2_PACKET_1    0x000000000000
#define SECPLUS_V2_PACKET_2    0x010000000000
#define SECPLUS_V2_PACKET_MASK 0x30000000000

static const SubGhzBlockConst subghz_protocol_secplus_v2_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 110,
    .min_count_bit_for_found = 62,
};

struct SubGhzProtocolDecoderSecPlus_v2 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_saved_state;
    uint64_t secplus_packet_1;
};

struct SubGhzProtocolEncoderSecPlus_v2 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    uint64_t secplus_packet_1;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderSecPlus_v2);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderSecPlus_v2);

typedef enum {
    SecPlus_v2DecoderStepReset = 0,
    SecPlus_v2DecoderStepDecoderData,
} SecPlus_v2DecoderStep;

const SubGhzProtocolDecoder subghz_protocol_secplus_v2_decoder = {
    .alloc = subghz_protocol_decoder_secplus_v2_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_secplus_v2_feed,
    .reset = subghz_protocol_decoder_secplus_v2_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_secplus_v2_serialize,
    .deserialize = subghz_protocol_decoder_secplus_v2_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

/* host bridge: firmware functions/state the SD encoder uses (handed over at load) */
typedef struct {
    void* secplus_v2_raw_replay;
} enc_secplus_v2_host_t;
static const enc_secplus_v2_host_t enc_secplus_v2_host = {
    .secplus_v2_raw_replay = (void*)&secplus_v2_raw_replay,
};
static const SubGhzEncoderExternal subghz_protocol_secplus_v2_encoder_ext = SUBGHZ_ENCODER_EXTERNAL_HOST("enc_secplus_v2", &enc_secplus_v2_host);

const SubGhzProtocol subghz_protocol_secplus_v2 = {
    .name = SUBGHZ_PROTOCOL_SECPLUS_V2_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,

    .decoder = &subghz_protocol_secplus_v2_decoder,
    .encoder = &subghz_protocol_secplus_v2_encoder_ext.vtable,
};

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

static bool subghz_protocol_secplus_v2_mix_order_encode(uint8_t order, uint16_t p[]) {
    uint16_t a, b, c;

    switch(order) {
    case 0x06:
    case 0x09:
        a = p[2];
        b = p[1];
        c = p[0];
        break;
    case 0x08:
    case 0x04:
        a = p[1];
        b = p[2];
        c = p[0];
        break;
    case 0x01:
        a = p[2];
        b = p[0];
        c = p[1];
        break;
    case 0x00:
        a = p[0];
        b = p[2];
        c = p[1];
        break;
    case 0x05:
        a = p[1];
        b = p[0];
        c = p[2];
        break;
    case 0x02:
    case 0x0A:
        a = p[0];
        b = p[1];
        c = p[2];
        break;
    default:
        FURI_LOG_E(TAG, "Order FAIL");
        return false;
    }

    p[0] = a;
    p[1] = b;
    p[2] = c;
    return true;
}

static uint64_t subghz_protocol_secplus_v2_encode_half(uint8_t roll_array[], uint32_t fixed) {
    uint64_t data = 0;
    uint16_t p[3] = {(fixed >> 10) & 0x3FF, fixed & 0x3FF, 0};
    uint8_t order = roll_array[0] << 2 | roll_array[1];
    uint8_t invert = roll_array[2] << 2 | roll_array[3];
    p[2] = (uint16_t)roll_array[4] << 8 | roll_array[5] << 6 | roll_array[6] << 4 |
           roll_array[7] << 2 | roll_array[8];

    if(!subghz_protocol_secplus_v2_mix_order_encode(order, p)) return 0;
    if(!subghz_protocol_secplus_v2_mix_invet(invert, p)) return 0;

    for(int i = 0; i < 10; i++) {
        data <<= 3;
        data |= bit_read(p[0], 9 - i) << 2 | bit_read(p[1], 9 - i) << 1 | bit_read(p[2], 9 - i);
    }
    data |= ((uint64_t)order) << 34 | ((uint64_t)invert) << 30;

    return data;
}

static uint8_t subghz_protocol_secplus_v2_get_btn_code(void);

static void subghz_protocol_secplus_v2_encode(SubGhzProtocolEncoderSecPlus_v2* instance) {

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->generic.btn);
    }

    instance->generic.btn = subghz_protocol_secplus_v2_get_btn_code();

    if(subghz_block_generic_global_button_override_get(&instance->generic.btn))
        FURI_LOG_D(TAG, "Button sucessfully changed to 0x%X", instance->generic.btn);

    uint32_t fixed_1[1] = {instance->generic.btn << 12 | instance->generic.serial >> 20};
    uint32_t fixed_2[1] = {instance->generic.serial & 0xFFFFF};
    uint8_t rolling_digits[18] = {0};
    uint8_t roll_1[9] = {0};
    uint8_t roll_2[9] = {0};

    if(secplus_v2_raw_replay) {
        /* #16: freeze counter on TxRaw verbatim replay */
    } else if(furi_hal_subghz_get_rolling_counter_mult() != -0x7FFFFFFF) {

        if(!subghz_block_generic_global_counter_override_get(&instance->generic.cnt)) {

            if((instance->generic.cnt + furi_hal_subghz_get_rolling_counter_mult()) > 0xFFFFFFF) {
                instance->generic.cnt = 0xE500000;
            } else {
                instance->generic.cnt += furi_hal_subghz_get_rolling_counter_mult();
            }
        }
        if(instance->generic.cnt < 0xE500000) instance->generic.cnt = 0xE500000;
    } else {

        if((instance->generic.cnt + 0x1) > 0xFFFFFFF) {
            instance->generic.cnt = 0xE500000;
        } else if(instance->generic.cnt >= 0xE500000 && instance->generic.cnt != 0xFFFFFFE) {
            instance->generic.cnt = 0xFFFFFFE;
        } else {
            instance->generic.cnt++;
        }
    }

    uint32_t rolling = subghz_protocol_blocks_reverse_key(instance->generic.cnt, 28);

    for(int8_t i = 17; i > -1; i--) {
        rolling_digits[i] = rolling % 3;
        rolling /= 3;
    }

    roll_2[8] = rolling_digits[0];
    roll_1[8] = rolling_digits[1];

    roll_2[4] = rolling_digits[2];
    roll_2[5] = rolling_digits[3];
    roll_2[6] = rolling_digits[4];
    roll_2[7] = rolling_digits[5];

    roll_1[4] = rolling_digits[6];
    roll_1[5] = rolling_digits[7];
    roll_1[6] = rolling_digits[8];
    roll_1[7] = rolling_digits[9];

    roll_2[0] = rolling_digits[10];
    roll_2[1] = rolling_digits[11];
    roll_2[2] = rolling_digits[12];
    roll_2[3] = rolling_digits[13];

    roll_1[0] = rolling_digits[14];
    roll_1[1] = rolling_digits[15];
    roll_1[2] = rolling_digits[16];
    roll_1[3] = rolling_digits[17];

    instance->secplus_packet_1 = SECPLUS_V2_HEADER | SECPLUS_V2_PACKET_1 |
                                 subghz_protocol_secplus_v2_encode_half(roll_1, fixed_1[0]);
    instance->generic.data = SECPLUS_V2_HEADER | SECPLUS_V2_PACKET_2 |
                             subghz_protocol_secplus_v2_encode_half(roll_2, fixed_2[0]);
}

bool subghz_protocol_secplus_v2_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint32_t cnt,
    SubGhzRadioPreset* preset) {
    furi_check(context);

    SubGhzProtocolEncoderSecPlus_v2* instance = context;
    instance->generic.serial = serial;
    instance->generic.cnt = cnt;
    instance->generic.btn = btn;
    instance->generic.data_count_bit =
        (uint8_t)subghz_protocol_secplus_v2_const.min_count_bit_for_found;
    subghz_protocol_secplus_v2_encode(instance);
    SubGhzProtocolStatus res =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    uint8_t key_data[sizeof(uint64_t)] = {0};
    for(size_t i = 0; i < sizeof(uint64_t); i++) {
        key_data[sizeof(uint64_t) - i - 1] = (instance->secplus_packet_1 >> (i * 8)) & 0xFF;
    }

    if((res == SubGhzProtocolStatusOk) &&
       !flipper_format_write_hex(flipper_format, "Secplus_packet_1", key_data, sizeof(uint64_t))) {
        FURI_LOG_E(TAG, "Unable to add Secplus_packet_1");
        res = SubGhzProtocolStatusErrorParserOthers;
    }
    return res == SubGhzProtocolStatusOk;
}

void* subghz_protocol_decoder_secplus_v2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderSecPlus_v2), &subghz_protocol_secplus_v2);
}

void subghz_protocol_decoder_secplus_v2_reset(void* context) {
    furi_assert(context);

}

static bool subghz_protocol_secplus_v2_check_packet(SubGhzProtocolDecoderSecPlus_v2* instance) {
    if((instance->decoder.decode_data & SECPLUS_V2_HEADER_MASK) == SECPLUS_V2_HEADER) {
        if((instance->decoder.decode_data & SECPLUS_V2_PACKET_MASK) == SECPLUS_V2_PACKET_1) {
            instance->secplus_packet_1 = instance->decoder.decode_data;
        } else if(
            ((instance->decoder.decode_data & SECPLUS_V2_PACKET_MASK) == SECPLUS_V2_PACKET_2) &&
            (instance->secplus_packet_1)) {
            return true;
        }
    }
    return false;
}

void subghz_protocol_decoder_secplus_v2_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderSecPlus_v2* instance = context;

    ManchesterEvent event = ManchesterEventReset;
    switch(instance->decoder.parser_step) {
    case SecPlus_v2DecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_secplus_v2_const.te_long * 130) <
                        subghz_protocol_secplus_v2_const.te_delta * 100)) {

            instance->decoder.parser_step = SecPlus_v2DecoderStepDecoderData;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->secplus_packet_1 = 0;
            manchester_advance(
                instance->manchester_saved_state,
                ManchesterEventReset,
                &instance->manchester_saved_state,
                NULL);
            manchester_advance(
                instance->manchester_saved_state,
                ManchesterEventLongHigh,
                &instance->manchester_saved_state,
                NULL);
            manchester_advance(
                instance->manchester_saved_state,
                ManchesterEventShortLow,
                &instance->manchester_saved_state,
                NULL);
        }
        break;
    case SecPlus_v2DecoderStepDecoderData:
        if(!level) {
            if(DURATION_DIFF(duration, subghz_protocol_secplus_v2_const.te_short) <
               subghz_protocol_secplus_v2_const.te_delta) {
                event = ManchesterEventShortLow;
            } else if(
                DURATION_DIFF(duration, subghz_protocol_secplus_v2_const.te_long) <
                subghz_protocol_secplus_v2_const.te_delta) {
                event = ManchesterEventLongLow;
            } else if(
                duration >= (subghz_protocol_secplus_v2_const.te_long * 2UL +
                             subghz_protocol_secplus_v2_const.te_delta)) {
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_secplus_v2_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                    if(subghz_protocol_secplus_v2_check_packet(instance)) {
                        if(instance->base.callback)
                            instance->base.callback(&instance->base, instance->base.context);
                        instance->decoder.parser_step = SecPlus_v2DecoderStepReset;
                    }
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                manchester_advance(
                    instance->manchester_saved_state,
                    ManchesterEventReset,
                    &instance->manchester_saved_state,
                    NULL);
                manchester_advance(
                    instance->manchester_saved_state,
                    ManchesterEventLongHigh,
                    &instance->manchester_saved_state,
                    NULL);
                manchester_advance(
                    instance->manchester_saved_state,
                    ManchesterEventShortLow,
                    &instance->manchester_saved_state,
                    NULL);
            } else {
                instance->decoder.parser_step = SecPlus_v2DecoderStepReset;
            }
        } else {
            if(DURATION_DIFF(duration, subghz_protocol_secplus_v2_const.te_short) <
               subghz_protocol_secplus_v2_const.te_delta) {
                event = ManchesterEventShortHigh;
            } else if(
                DURATION_DIFF(duration, subghz_protocol_secplus_v2_const.te_long) <
                subghz_protocol_secplus_v2_const.te_delta) {
                event = ManchesterEventLongHigh;
            } else {
                instance->decoder.parser_step = SecPlus_v2DecoderStepReset;
            }
        }
        if(event != ManchesterEventReset) {
            bool data;
            bool data_ok = manchester_advance(
                instance->manchester_saved_state, event, &instance->manchester_saved_state, &data);

            if(data_ok) {
                instance->decoder.decode_data = (instance->decoder.decode_data << 1) | data;
                instance->decoder.decode_count_bit++;
            }
        }
        break;
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_secplus_v2_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderSecPlus_v2* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    uint8_t key_data[sizeof(uint64_t)] = {0};
    for(size_t i = 0; i < sizeof(uint64_t); i++) {
        key_data[sizeof(uint64_t) - i - 1] = (instance->secplus_packet_1 >> (i * 8)) & 0xFF;
    }

    if((ret == SubGhzProtocolStatusOk) &&
       !flipper_format_write_hex(flipper_format, "Secplus_packet_1", key_data, sizeof(uint64_t))) {
        FURI_LOG_E(TAG, "Unable to add Secplus_packet_1");
        ret = SubGhzProtocolStatusErrorParserOthers;
    }
    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_secplus_v2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderSecPlus_v2* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_secplus_v2_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        uint8_t key_data[sizeof(uint64_t)] = {0};
        if(!flipper_format_read_hex(
               flipper_format, "Secplus_packet_1", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Missing Secplus_packet_1");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        for(uint8_t i = 0; i < sizeof(uint64_t); i++) {
            instance->secplus_packet_1 = instance->secplus_packet_1 << 8 | key_data[i];
        }
    } while(false);

    return ret;
}

static uint8_t subghz_protocol_secplus_v2_get_btn_code(void) {
    uint8_t custom_btn_id = subghz_custom_btn_get();
    uint8_t original_btn_code = subghz_custom_btn_get_original();
    uint8_t btn = original_btn_code;

    if((custom_btn_id == SUBGHZ_CUSTOM_BTN_OK) && (original_btn_code != 0)) {

        btn = original_btn_code;
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_UP) {
        switch(original_btn_code) {
        case 0x68:
            btn = 0x80;
            break;
        case 0x80:
            btn = 0x68;
            break;
        case 0x81:
            btn = 0x80;
            break;
        case 0xE2:
            btn = 0x80;
            break;
        case 0x78:
            btn = 0x80;
            break;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_DOWN) {
        switch(original_btn_code) {
        case 0x68:
            btn = 0x81;
            break;
        case 0x80:
            btn = 0x81;
            break;
        case 0x81:
            btn = 0x68;
            break;
        case 0xE2:
            btn = 0x81;
            break;
        case 0x78:
            btn = 0x81;
            break;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_LEFT) {
        switch(original_btn_code) {
        case 0x68:
            btn = 0xE2;
            break;
        case 0x80:
            btn = 0xE2;
            break;
        case 0x81:
            btn = 0xE2;
            break;
        case 0xE2:
            btn = 0x68;
            break;
        case 0x78:
            btn = 0xE2;
            break;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_RIGHT) {
        switch(original_btn_code) {
        case 0x68:
            btn = 0x78;
            break;
        case 0x80:
            btn = 0x78;
            break;
        case 0x81:
            btn = 0x78;
            break;
        case 0xE2:
            btn = 0x78;
            break;
        case 0x78:
            btn = 0x68;
            break;

        default:
            break;
        }
    }

    return btn;
}

