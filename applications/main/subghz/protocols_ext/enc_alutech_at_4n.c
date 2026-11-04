/* enc_alutech_at_4n.fal : SD encoder for alutech_at_4n (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_alutech_at_4n enc_alutech_at_4n__subghz_protocol_alutech_at_4n
#define subghz_protocol_encoder_alutech_at_4n_alloc enc_alutech_at_4n__subghz_protocol_encoder_alutech_at_4n_alloc
#define subghz_protocol_encoder_alutech_at_4n_deserialize enc_alutech_at_4n__subghz_protocol_encoder_alutech_at_4n_deserialize
#define subghz_protocol_encoder_common_free enc_alutech_at_4n__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_alutech_at_4n__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_alutech_at_4n__subghz_protocol_encoder_common_yield
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/alutech_at_4n.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocoAlutech_at_4n"
#define SUBGHZ_NO_ALUTECH_AT_4N_RAINBOW_TABLE         0xFFFFFFFFFFFFFFFF
#define SUBGHZ_ALUTECH_AT_4N_RAINBOW_TABLE_SIZE_BYTES 32

struct SubGhzProtocolEncoderAlutech_at_4n {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    const char* alutech_at_4n_rainbow_table_file_name;
    uint32_t crc;
};

static const SubGhzBlockConst subghz_protocol_alutech_at_4n_const = {
    .te_short = 400,
    .te_long = 800,
    .te_delta = 140,
    .min_count_bit_for_found = 72,
};

/* host bridge: live firmware functions/state this encoder needs (set by the loader) */
extern uint8_t alutech_at4n_counter_mode__bridge_ty;
extern bool alutech_at_4n_raw_replay__bridge_ty;
extern bool bypass__bridge_ty;
typedef struct {
    void* alutech_at4n_counter_mode;
    void* alutech_at_4n_raw_replay;
    void* bypass;
} enc_alutech_at_4n_host_t;
static const void* enc_alutech_at_4n_host_raw;
#define ENC_ALUTECH_AT_4N_HOST ((const enc_alutech_at_4n_host_t*)enc_alutech_at_4n_host_raw)
#define alutech_at4n_counter_mode (*(__typeof__(alutech_at4n_counter_mode__bridge_ty)*)(ENC_ALUTECH_AT_4N_HOST->alutech_at4n_counter_mode))
#define alutech_at_4n_raw_replay (*(__typeof__(alutech_at_4n_raw_replay__bridge_ty)*)(ENC_ALUTECH_AT_4N_HOST->alutech_at_4n_raw_replay))
#define bypass (*(__typeof__(bypass__bridge_ty)*)(ENC_ALUTECH_AT_4N_HOST->bypass))

static uint32_t subghz_protocol_alutech_at_4n_get_magic_data_from_buffer(
    uint8_t* buffer,
    uint8_t number_alutech_at_4n_magic_data);
static uint8_t subghz_protocol_alutech_at_4n_crc(uint64_t data);
static uint8_t subghz_protocol_alutech_at_4n_decrypt_data_crc(uint8_t data);
static uint64_t subghz_protocol_alutech_at_4n_decrypt(uint64_t data, const char* file_name);
static uint64_t subghz_protocol_alutech_at_4n_encrypt(uint64_t data, const char* file_name);
static bool subghz_protocol_alutech_at_4n_gen_data(
    SubGhzProtocolEncoderAlutech_at_4n* instance,
    uint8_t btn);
static bool subghz_protocol_encoder_alutech_at_4n_get_upload(
    SubGhzProtocolEncoderAlutech_at_4n* instance,
    uint8_t btn);
static void subghz_protocol_alutech_at_4n_remote_controller(
    SubGhzBlockGeneric* instance,
    uint8_t crc,
    const char* file_name);
static uint8_t subghz_protocol_alutech_at_4n_get_btn_code(void);

static uint32_t subghz_protocol_alutech_at_4n_get_magic_data_from_buffer(
    uint8_t* buffer,
    uint8_t number_alutech_at_4n_magic_data) {
    uint32_t address = number_alutech_at_4n_magic_data * sizeof(uint32_t);
    uint32_t alutech_at_4n_magic_data = 0;

    for(size_t i = address; i < (address + sizeof(uint32_t)); i++) {
        alutech_at_4n_magic_data = (alutech_at_4n_magic_data << 8) | buffer[i];
    }

    return alutech_at_4n_magic_data;
}

static uint8_t subghz_protocol_alutech_at_4n_crc(uint64_t data) {
    uint8_t* p = (uint8_t*)&data;
    uint8_t crc = 0xff;
    for(uint8_t y = 0; y < 8; y++) {
        crc = crc ^ p[y];
        for(uint8_t i = 0; i < 8; i++) {
            if((crc & 0x80) != 0) {
                crc <<= 1;
                crc ^= 0x31;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static uint8_t subghz_protocol_alutech_at_4n_decrypt_data_crc(uint8_t data) {
    uint8_t crc = data;
    for(uint8_t i = 0; i < 8; i++) {
        if((crc & 0x80) != 0) {
            crc <<= 1;
            crc ^= 0x31;
        } else {
            crc <<= 1;
        }
    }
    return ~crc;
}

static uint64_t subghz_protocol_alutech_at_4n_decrypt(uint64_t data, const char* file_name) {

    if(!file_name) return SUBGHZ_NO_ALUTECH_AT_4N_RAINBOW_TABLE;

    uint8_t buffer[SUBGHZ_ALUTECH_AT_4N_RAINBOW_TABLE_SIZE_BYTES] = {0};
    uint8_t* buffer_ptr = (uint8_t*)&buffer;

    if(subghz_keystore_raw_get_data(
           file_name, 0, buffer, SUBGHZ_ALUTECH_AT_4N_RAINBOW_TABLE_SIZE_BYTES)) {
    } else {
        return SUBGHZ_NO_ALUTECH_AT_4N_RAINBOW_TABLE;
    }

    uint8_t* p = (uint8_t*)&data;
    uint32_t data1 = p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
    uint32_t data2 = p[4] << 24 | p[5] << 16 | p[6] << 8 | p[7];
    uint32_t data3 = 0;
    uint32_t magic_data[] = {
        subghz_protocol_alutech_at_4n_get_magic_data_from_buffer(buffer_ptr, 0),
        subghz_protocol_alutech_at_4n_get_magic_data_from_buffer(buffer_ptr, 1),
        subghz_protocol_alutech_at_4n_get_magic_data_from_buffer(buffer_ptr, 2),
        subghz_protocol_alutech_at_4n_get_magic_data_from_buffer(buffer_ptr, 3),
        subghz_protocol_alutech_at_4n_get_magic_data_from_buffer(buffer_ptr, 4),
        subghz_protocol_alutech_at_4n_get_magic_data_from_buffer(buffer_ptr, 5)};

    uint32_t i = magic_data[0];
    do {
        data2 = data2 -
                ((magic_data[1] + (data1 << 4)) ^ ((magic_data[2] + (data1 >> 5)) ^ (data1 + i)));
        data3 = data2 + i;
        i += magic_data[3];
        data1 =
            data1 - ((magic_data[4] + (data2 << 4)) ^ ((magic_data[5] + (data2 >> 5)) ^ data3));
    } while(i != 0);

    p[0] = (uint8_t)(data1 >> 24);
    p[1] = (uint8_t)(data1 >> 16);
    p[3] = (uint8_t)data1;
    p[4] = (uint8_t)(data2 >> 24);
    p[5] = (uint8_t)(data2 >> 16);
    p[2] = (uint8_t)(data1 >> 8);
    p[6] = (uint8_t)(data2 >> 8);
    p[7] = (uint8_t)data2;

    return data;
}

static uint64_t subghz_protocol_alutech_at_4n_encrypt(uint64_t data, const char* file_name) {

    if(!file_name) return SUBGHZ_NO_ALUTECH_AT_4N_RAINBOW_TABLE;

    uint8_t buffer[SUBGHZ_ALUTECH_AT_4N_RAINBOW_TABLE_SIZE_BYTES] = {0};
    uint8_t* buffer_ptr = (uint8_t*)&buffer;

    if(subghz_keystore_raw_get_data(
           file_name, 0, buffer, SUBGHZ_ALUTECH_AT_4N_RAINBOW_TABLE_SIZE_BYTES)) {
    } else {
        return SUBGHZ_NO_ALUTECH_AT_4N_RAINBOW_TABLE;
    }

    uint8_t* p = (uint8_t*)&data;
    uint32_t data1 = 0;
    uint32_t data2 = p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
    uint32_t data3 = p[4] << 24 | p[5] << 16 | p[6] << 8 | p[7];
    uint32_t magic_data[] = {
        subghz_protocol_alutech_at_4n_get_magic_data_from_buffer(buffer_ptr, 6),
        subghz_protocol_alutech_at_4n_get_magic_data_from_buffer(buffer_ptr, 4),
        subghz_protocol_alutech_at_4n_get_magic_data_from_buffer(buffer_ptr, 5),
        subghz_protocol_alutech_at_4n_get_magic_data_from_buffer(buffer_ptr, 1),
        subghz_protocol_alutech_at_4n_get_magic_data_from_buffer(buffer_ptr, 2),
        subghz_protocol_alutech_at_4n_get_magic_data_from_buffer(buffer_ptr, 0)};

    do {
        data1 = data1 + magic_data[0];
        data2 = data2 + ((magic_data[1] + (data3 << 4)) ^
                         ((magic_data[2] + (data3 >> 5)) ^ (data1 + data3)));
        data3 = data3 + ((magic_data[3] + (data2 << 4)) ^
                         ((magic_data[4] + (data2 >> 5)) ^ (data1 + data2)));
    } while(data1 != magic_data[5]);
    p[0] = (uint8_t)(data2 >> 24);
    p[1] = (uint8_t)(data2 >> 16);
    p[3] = (uint8_t)data2;
    p[4] = (uint8_t)(data3 >> 24);
    p[5] = (uint8_t)(data3 >> 16);
    p[2] = (uint8_t)(data2 >> 8);
    p[6] = (uint8_t)(data3 >> 8);
    p[7] = (uint8_t)data3;

    return data;
}

static bool subghz_protocol_alutech_at_4n_gen_data(
    SubGhzProtocolEncoderAlutech_at_4n* instance,
    uint8_t btn) {
    uint64_t data = subghz_protocol_blocks_reverse_key(instance->generic.data, 64);

    data = subghz_protocol_alutech_at_4n_decrypt(
        data, instance->alutech_at_4n_rainbow_table_file_name);
    uint8_t crc = data >> 56;
    if(crc == subghz_protocol_alutech_at_4n_decrypt_data_crc((uint8_t)((data >> 8) & 0xFF))) {
        instance->generic.btn = (uint8_t)data & 0xFF;
        instance->generic.cnt = (uint16_t)(data >> 8) & 0xFFFF;
        instance->generic.serial = (uint32_t)(data >> 24) & 0xFFFFFFFF;
    }

    if(subghz_block_generic_global.cnt_need_override) bypass = true;

    if((alutech_at4n_counter_mode == 0) || bypass) {

        if(alutech_at_4n_raw_replay) {
            bypass = false; /* #16: freeze counter on TxRaw verbatim replay */
        } else if((furi_hal_subghz_get_rolling_counter_mult() != -0x7FFFFFFF) || bypass) {
            bypass = false;

            if(!subghz_block_generic_global_counter_override_get(&instance->generic.cnt)) {

                if((instance->generic.cnt + furi_hal_subghz_get_rolling_counter_mult()) > 0xFFFF) {
                    instance->generic.cnt = 0;
                } else {
                    instance->generic.cnt += furi_hal_subghz_get_rolling_counter_mult();
                }
            }
        } else {

            if((instance->generic.cnt + 0x1) > 0xFFFF) {
                instance->generic.cnt = 0;
            } else if(instance->generic.cnt >= 0x1 && instance->generic.cnt != 0xFFFE) {
                instance->generic.cnt = 0xFFFE;
            } else {
                instance->generic.cnt++;
            }
        }
    } else if(alutech_at4n_counter_mode == 1) {

        if((instance->generic.cnt + 0x1) > 0xFFFF) {
            instance->generic.cnt = 0;
        } else if(instance->generic.cnt >= 0x1 && instance->generic.cnt != 0xFFFE) {
            instance->generic.cnt = 0xFFFE;
        } else {
            instance->generic.cnt++;
        }
    } else {

        if(instance->generic.cnt >= 0x0005) {
            instance->generic.cnt = 0;
        } else {
            instance->generic.cnt++;
        }
    }
    crc = subghz_protocol_alutech_at_4n_decrypt_data_crc((uint8_t)(instance->generic.cnt & 0xFF));
    data = (uint64_t)crc << 56 | (uint64_t)instance->generic.serial << 24 |
           (uint32_t)instance->generic.cnt << 8 | btn;

    data = subghz_protocol_alutech_at_4n_encrypt(
        data, instance->alutech_at_4n_rainbow_table_file_name);
    crc = subghz_protocol_alutech_at_4n_crc(data);
    instance->generic.data = subghz_protocol_blocks_reverse_key(data, 64);
    instance->crc = subghz_protocol_blocks_reverse_key(crc, 8);
    return true;
}

static bool subghz_protocol_encoder_alutech_at_4n_get_upload(
    SubGhzProtocolEncoderAlutech_at_4n* instance,
    uint8_t btn) {
    furi_assert(instance);

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(btn);
    }

    btn = subghz_protocol_alutech_at_4n_get_btn_code();

    if(subghz_block_generic_global_button_override_get(&btn)) {
        bypass = true;
        FURI_LOG_D(TAG, "Button sucessfully changed to 0x%X", btn);
    }

    if(!subghz_protocol_alutech_at_4n_gen_data(instance, btn)) {
        return false;
    }

    size_t index = 0;

    for(uint8_t i = 0; i < 12; ++i) {
        instance->encoder.upload[index++] =
            level_duration_make(true, (uint32_t)subghz_protocol_alutech_at_4n_const.te_short);
        instance->encoder.upload[index++] = level_duration_make(
            false, (uint32_t)subghz_protocol_alutech_at_4n_const.te_short);
    }

    instance->encoder.upload[index - 1].duration +=
        (uint32_t)subghz_protocol_alutech_at_4n_const.te_short * 9;

    for(uint8_t i = 64; i > 0; --i) {
        if(bit_read(instance->generic.data, i - 1)) {

            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_alutech_at_4n_const.te_short);
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_alutech_at_4n_const.te_long);
        } else {

            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_alutech_at_4n_const.te_long);
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_alutech_at_4n_const.te_short);
        }
    }

    for(uint8_t i = 8; i > 0; --i) {
        if(bit_read(instance->crc, i - 1)) {

            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_alutech_at_4n_const.te_short);
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_alutech_at_4n_const.te_long);
        } else {

            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_alutech_at_4n_const.te_long);
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_alutech_at_4n_const.te_short);
        }
    }

    instance->encoder.upload[index - 1].duration +=
        (uint32_t)subghz_protocol_alutech_at_4n_const.te_long * 20;

    size_t size_upload = index;

    if(size_upload > instance->encoder.size_upload) {
        FURI_LOG_E(TAG, "Size upload exceeds allocated encoder buffer.");
        return false;
    } else {
        instance->encoder.size_upload = size_upload;
    }
    return true;
}

static void subghz_protocol_alutech_at_4n_remote_controller(
    SubGhzBlockGeneric* instance,
    uint8_t crc,
    const char* file_name) {

    bool status = false;
    uint64_t data = subghz_protocol_blocks_reverse_key(instance->data, 64);
    crc = subghz_protocol_blocks_reverse_key(crc, 8);

    if(crc == subghz_protocol_alutech_at_4n_crc(data)) {
        data = subghz_protocol_alutech_at_4n_decrypt(data, file_name);
        status = true;
    }

    if(status && ((uint8_t)(data >> 56) ==
                  subghz_protocol_alutech_at_4n_decrypt_data_crc((uint8_t)((data >> 8) & 0xFF)))) {
        instance->btn = (uint8_t)data & 0xFF;
        instance->cnt = (uint16_t)(data >> 8) & 0xFFFF;
        instance->serial = (uint32_t)(data >> 24) & 0xFFFFFFFF;
    }

    if(!status) {
        instance->btn = 0;
        instance->cnt = 0;
        instance->serial = 0;
    }

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->btn);
    }
    subghz_custom_btn_set_max(4);
}

static uint8_t subghz_protocol_alutech_at_4n_get_btn_code(void) {
    uint8_t custom_btn_id = subghz_custom_btn_get();
    uint8_t original_btn_code = subghz_custom_btn_get_original();
    uint8_t btn = original_btn_code;

    if((custom_btn_id == SUBGHZ_CUSTOM_BTN_OK) && (original_btn_code != 0)) {

        btn = original_btn_code;
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_UP) {
        switch(original_btn_code) {
        case 0x11:
            btn = 0x22;
            break;
        case 0x22:
            btn = 0x11;
            break;
        case 0xFF:
            btn = 0x11;
            break;
        case 0x44:
            btn = 0x11;
            break;
        case 0x33:
            btn = 0x11;
            break;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_DOWN) {
        switch(original_btn_code) {
        case 0x11:
            btn = 0x44;
            break;
        case 0x22:
            btn = 0x44;
            break;
        case 0xFF:
            btn = 0x44;
            break;
        case 0x44:
            btn = 0xFF;
            break;
        case 0x33:
            btn = 0x44;
            break;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_LEFT) {
        switch(original_btn_code) {
        case 0x11:
            btn = 0x33;
            break;
        case 0x22:
            btn = 0x33;
            break;
        case 0xFF:
            btn = 0x33;
            break;
        case 0x44:
            btn = 0x33;
            break;
        case 0x33:
            btn = 0x22;
            break;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_RIGHT) {
        switch(original_btn_code) {
        case 0x11:
            btn = 0xFF;
            break;
        case 0x22:
            btn = 0xFF;
            break;
        case 0xFF:
            btn = 0x22;
            break;
        case 0x44:
            btn = 0x22;
            break;
        case 0x33:
            btn = 0xFF;
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
void subghz_protocol_encoder_common_stop(void* context) {
    SubGhzProtocolEncoderCommon* instance = context;
    instance->encoder.is_running = false;
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

void* subghz_protocol_encoder_alutech_at_4n_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderAlutech_at_4n* instance =
        malloc(sizeof(SubGhzProtocolEncoderAlutech_at_4n));

    instance->base.protocol = &subghz_protocol_alutech_at_4n;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 3;
    instance->encoder.size_upload = 512;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;

    instance->alutech_at_4n_rainbow_table_file_name =
        subghz_environment_get_alutech_at_4n_rainbow_table_file_name(environment);
    if(instance->alutech_at_4n_rainbow_table_file_name) {
        FURI_LOG_I(
            TAG, "Loading rainbow table from %s", instance->alutech_at_4n_rainbow_table_file_name);
    }

    return instance;
}

SubGhzProtocolStatus subghz_protocol_encoder_alutech_at_4n_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderAlutech_at_4n* instance = context;
    SubGhzProtocolStatus res = SubGhzProtocolStatusError;
    do {
        /* #16: TxRaw verbatim replay — freeze the rolling counter. */
        alutech_at_4n_raw_replay = false;
        {
            uint32_t alutech_at_4n_txraw = 0;
            flipper_format_rewind(flipper_format);
            if(flipper_format_read_uint32(flipper_format, "TxRaw", &alutech_at_4n_txraw, 1) &&
               alutech_at_4n_txraw != 0) {
                alutech_at_4n_raw_replay = true;
            }
            flipper_format_rewind(flipper_format);
        }
        if(SubGhzProtocolStatusOk !=
           subghz_block_generic_deserialize(&instance->generic, flipper_format)) {
            FURI_LOG_E(TAG, "Deserialize error");
            break;
        }

        if(!flipper_format_read_uint32(flipper_format, "CRC", (uint32_t*)&instance->crc, 1)) {
            FURI_LOG_E(TAG, "Missing CRC");
            break;
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }

        uint32_t tmp_counter_mode;
        if(flipper_format_read_uint32(flipper_format, "CounterMode", &tmp_counter_mode, 1)) {
            alutech_at4n_counter_mode = (uint8_t)tmp_counter_mode;
        } else {
            alutech_at4n_counter_mode = 0;
        }

        subghz_protocol_alutech_at_4n_remote_controller(
            &instance->generic, instance->crc, instance->alutech_at_4n_rainbow_table_file_name);

        subghz_protocol_encoder_alutech_at_4n_get_upload(instance, instance->generic.btn);

        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }
        uint8_t key_data[sizeof(uint64_t)] = {0};
        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            key_data[sizeof(uint64_t) - i - 1] = (instance->generic.data >> i * 8) & 0xFF;
        }
        if(!flipper_format_update_hex(flipper_format, "Key", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Unable to add Key");
            break;
        }
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }
        if(!flipper_format_update_uint32(flipper_format, "CRC", &instance->crc, 1)) {
            FURI_LOG_E(TAG, "Unable to add CRC");
            break;
        }

        instance->encoder.is_running = true;

        res = SubGhzProtocolStatusOk;
    } while(false);

    return res;
}

static const SubGhzProtocolEncoder enc_alutech_at_4n_vt = {
    .alloc = subghz_protocol_encoder_alutech_at_4n_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_alutech_at_4n_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_alutech_at_4n = {
    .name = SUBGHZ_PROTOCOL_ALUTECH_AT_4N_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = NULL,
    .encoder = &enc_alutech_at_4n_vt,
};

static const SubGhzEncodePluginEntry enc_alutech_at_4n_entry = {.name = SUBGHZ_PROTOCOL_ALUTECH_AT_4N_NAME, .encoder = &enc_alutech_at_4n_vt, .host_slot = &enc_alutech_at_4n_host_raw};
static const FlipperAppPluginDescriptor enc_alutech_at_4n_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_alutech_at_4n_entry};
const FlipperAppPluginDescriptor* enc_alutech_at_4n_ep(void) { return &enc_alutech_at_4n_descriptor; }
