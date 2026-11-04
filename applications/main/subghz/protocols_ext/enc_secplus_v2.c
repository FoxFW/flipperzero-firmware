/* enc_secplus_v2.fal : SD encoder for secplus_v2 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_alloc enc_secplus_v2__subghz_protocol_encoder_common_alloc
#define subghz_protocol_encoder_common_free enc_secplus_v2__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_yield enc_secplus_v2__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_secplus_v2_alloc enc_secplus_v2__subghz_protocol_encoder_secplus_v2_alloc
#define subghz_protocol_encoder_secplus_v2_deserialize enc_secplus_v2__subghz_protocol_encoder_secplus_v2_deserialize
#define subghz_protocol_encoder_secplus_v2_stop enc_secplus_v2__subghz_protocol_encoder_secplus_v2_stop
#define subghz_protocol_secplus_v2 enc_secplus_v2__subghz_protocol_secplus_v2
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
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
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocoSecPlusV2"
#define SECPLUS_V2_HEADER      0x3C0000000000
#define SECPLUS_V2_PACKET_1    0x000000000000
#define SECPLUS_V2_PACKET_2    0x010000000000

struct SubGhzProtocolEncoderSecPlus_v2 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    uint64_t secplus_packet_1;
};

static const SubGhzBlockConst subghz_protocol_secplus_v2_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 110,
    .min_count_bit_for_found = 62,
};

/* host bridge: live firmware functions/state this encoder needs (set by the loader) */
extern bool secplus_v2_raw_replay__bridge_ty;
typedef struct {
    void* secplus_v2_raw_replay;
} enc_secplus_v2_host_t;
static const void* enc_secplus_v2_host_raw;
#define ENC_SECPLUS_V2_HOST ((const enc_secplus_v2_host_t*)enc_secplus_v2_host_raw)
#define secplus_v2_raw_replay (*(__typeof__(secplus_v2_raw_replay__bridge_ty)*)(ENC_SECPLUS_V2_HOST->secplus_v2_raw_replay))

static bool subghz_protocol_secplus_v2_mix_invet(uint8_t invert, uint16_t p[]);
static bool subghz_protocol_secplus_v2_mix_order_decode(uint8_t order, uint16_t p[]);
static bool subghz_protocol_secplus_v2_mix_order_encode(uint8_t order, uint16_t p[]);
static bool
    subghz_protocol_secplus_v2_decode_half(uint64_t data, uint8_t roll_array[], uint32_t* fixed);
static void
    subghz_protocol_secplus_v2_remote_controller(SubGhzBlockGeneric* instance, uint64_t packet_1);
static uint64_t subghz_protocol_secplus_v2_encode_half(uint8_t roll_array[], uint32_t fixed);
static void subghz_protocol_secplus_v2_encode(SubGhzProtocolEncoderSecPlus_v2* instance);
static LevelDuration
    subghz_protocol_encoder_secplus_v2_add_duration_to_upload(ManchesterEncoderResult result);
static void
    subghz_protocol_encoder_secplus_v2_get_upload(SubGhzProtocolEncoderSecPlus_v2* instance);
static uint8_t subghz_protocol_secplus_v2_get_btn_code(void);

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

static LevelDuration
    subghz_protocol_encoder_secplus_v2_add_duration_to_upload(ManchesterEncoderResult result) {
    LevelDuration data = {.duration = 0, .level = 0};
    switch(result) {
    case ManchesterEncoderResultShortLow:
        data.duration = subghz_protocol_secplus_v2_const.te_short;
        data.level = false;
        break;
    case ManchesterEncoderResultLongLow:
        data.duration = subghz_protocol_secplus_v2_const.te_long;
        data.level = false;
        break;
    case ManchesterEncoderResultLongHigh:
        data.duration = subghz_protocol_secplus_v2_const.te_long;
        data.level = true;
        break;
    case ManchesterEncoderResultShortHigh:
        data.duration = subghz_protocol_secplus_v2_const.te_short;
        data.level = true;
        break;

    default:
        furi_crash("SubGhz: ManchesterEncoderResult is incorrect.");
        break;
    }
    return level_duration_make(data.level, data.duration);
}

static void
    subghz_protocol_encoder_secplus_v2_get_upload(SubGhzProtocolEncoderSecPlus_v2* instance) {
    furi_assert(instance);
    size_t index = 0;

    ManchesterEncoderState enc_state;
    manchester_encoder_reset(&enc_state);
    ManchesterEncoderResult result;

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if(!manchester_encoder_advance(
               &enc_state, bit_read(instance->secplus_packet_1, i - 1), &result)) {
            instance->encoder.upload[index++] =
                subghz_protocol_encoder_secplus_v2_add_duration_to_upload(result);
            manchester_encoder_advance(
                &enc_state, bit_read(instance->secplus_packet_1, i - 1), &result);
        }
        instance->encoder.upload[index++] =
            subghz_protocol_encoder_secplus_v2_add_duration_to_upload(result);
    }
    instance->encoder.upload[index] = subghz_protocol_encoder_secplus_v2_add_duration_to_upload(
        manchester_encoder_finish(&enc_state));
    if(level_duration_get_level(instance->encoder.upload[index])) {
        index++;
    }
    instance->encoder.upload[index++] =
        level_duration_make(false, (uint32_t)subghz_protocol_secplus_v2_const.te_long * 136);

    manchester_encoder_reset(&enc_state);
    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if(!manchester_encoder_advance(
               &enc_state, bit_read(instance->generic.data, i - 1), &result)) {
            instance->encoder.upload[index++] =
                subghz_protocol_encoder_secplus_v2_add_duration_to_upload(result);
            manchester_encoder_advance(
                &enc_state, bit_read(instance->generic.data, i - 1), &result);
        }
        instance->encoder.upload[index++] =
            subghz_protocol_encoder_secplus_v2_add_duration_to_upload(result);
    }
    instance->encoder.upload[index] = subghz_protocol_encoder_secplus_v2_add_duration_to_upload(
        manchester_encoder_finish(&enc_state));
    if(level_duration_get_level(instance->encoder.upload[index])) {
        index++;
    }
    instance->encoder.upload[index++] =
        level_duration_make(false, (uint32_t)subghz_protocol_secplus_v2_const.te_long * 136);

    instance->encoder.size_upload = index;
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

/* vendored from lib/subghz/protocols/common.c (not exported) */
void subghz_protocol_encoder_common_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderCommon* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

/* vendored from lib/subghz/protocols/common.c (not exported) */
LevelDuration subghz_protocol_encoder_common_yield(void* context) {
    SubGhzProtocolEncoderCommon* instance = context;

    if(instance->encoder.repeat == 0 || !instance->encoder.is_running) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }

    LevelDuration ret = instance->encoder.upload[instance->encoder.front];

    if(++instance->encoder.front == instance->encoder.size_upload) {
        if(!subghz_block_generic_global.endless_tx) instance->encoder.repeat--;
        instance->encoder.front = 0;
    }

    return ret;
}

/* vendored from lib/subghz/protocols/common.c (not exported) */
void* subghz_protocol_encoder_common_alloc(
    size_t instance_size,
    const SubGhzProtocol* protocol,
    size_t repeat,
    size_t size_upload) {
    SubGhzProtocolEncoderCommonGeneric* instance = malloc(instance_size);
    instance->common.base.protocol = protocol;
    instance->generic.protocol_name = protocol->name;
    instance->common.encoder.repeat = repeat;
    instance->common.encoder.size_upload = size_upload;
    instance->common.encoder.upload = malloc(size_upload * sizeof(LevelDuration));
    instance->common.encoder.is_running = false;
    return instance;
}

void* subghz_protocol_encoder_secplus_v2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderSecPlus_v2), &subghz_protocol_secplus_v2, 3, 256);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_secplus_v2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderSecPlus_v2* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        /* #16: TxRaw verbatim replay — freeze the rolling counter. */
        secplus_v2_raw_replay = false;
        {
            uint32_t secplus_v2_txraw = 0;
            flipper_format_rewind(flipper_format);
            if(flipper_format_read_uint32(flipper_format, "TxRaw", &secplus_v2_txraw, 1) &&
               secplus_v2_txraw != 0) {
                secplus_v2_raw_replay = true;
            }
            flipper_format_rewind(flipper_format);
        }
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_secplus_v2_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }
        uint8_t key_data[sizeof(uint64_t)] = {0};
        if(!flipper_format_read_hex(
               flipper_format, "Secplus_packet_1", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Secplus_packet_1");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        for(uint8_t i = 0; i < sizeof(uint64_t); i++) {
            instance->secplus_packet_1 = instance->secplus_packet_1 << 8 | key_data[i];
        }

        subghz_protocol_secplus_v2_remote_controller(
            &instance->generic, instance->secplus_packet_1);
        subghz_protocol_secplus_v2_encode(instance);

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);
        subghz_protocol_encoder_secplus_v2_get_upload(instance);

        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            key_data[sizeof(uint64_t) - i - 1] = (instance->generic.data >> (i * 8)) & 0xFF;
        }
        if(!flipper_format_update_hex(flipper_format, "Key", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Unable to add Key");
            ret = SubGhzProtocolStatusErrorParserKey;
            break;
        }

        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            key_data[sizeof(uint64_t) - i - 1] = (instance->secplus_packet_1 >> (i * 8)) & 0xFF;
        }
        if(!flipper_format_update_hex(
               flipper_format, "Secplus_packet_1", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Unable to add Secplus_packet_1");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }

        instance->encoder.front = 0;
        instance->encoder.is_running = true;
    } while(false);

    return ret;
}

void subghz_protocol_encoder_secplus_v2_stop(void* context) {
    SubGhzProtocolEncoderSecPlus_v2* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
}

static const SubGhzProtocolEncoder enc_secplus_v2_vt = {
    .alloc = subghz_protocol_encoder_secplus_v2_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_secplus_v2_deserialize,
    .stop = subghz_protocol_encoder_secplus_v2_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_secplus_v2 = {
    .name = SUBGHZ_PROTOCOL_SECPLUS_V2_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = NULL,
    .encoder = &enc_secplus_v2_vt,
};

static const SubGhzEncodePluginEntry enc_secplus_v2_entry = {.name = SUBGHZ_PROTOCOL_SECPLUS_V2_NAME, .encoder = &enc_secplus_v2_vt, .host_slot = &enc_secplus_v2_host_raw};
static const FlipperAppPluginDescriptor enc_secplus_v2_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_secplus_v2_entry};
const FlipperAppPluginDescriptor* enc_secplus_v2_ep(void) { return &enc_secplus_v2_descriptor; }
