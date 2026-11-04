/* enc_nice_flor_s.fal : SD encoder for nice_flor_s (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_free enc_nice_flor_s__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_nice_flor_s__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_nice_flor_s__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_nice_flor_s_alloc enc_nice_flor_s__subghz_protocol_encoder_nice_flor_s_alloc
#define subghz_protocol_encoder_nice_flor_s_deserialize enc_nice_flor_s__subghz_protocol_encoder_nice_flor_s_deserialize
#define subghz_protocol_nice_flor_s enc_nice_flor_s__subghz_protocol_nice_flor_s
#define subghz_protocol_nice_flor_s_encrypt enc_nice_flor_s__subghz_protocol_nice_flor_s_encrypt
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/nice_flor_s.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/common.h>
#include <lib/subghz/protocols/nice_flor_s_o.h>

#define TAG "SubGhzProtocolNiceFlorS"
#define SUBGHZ_NICE_FLOR_S_RAINBOW_TABLE_SIZE_BYTES 32
#define SUBGHZ_NO_NICE_FLOR_S_RAINBOW_TABLE         0

struct SubGhzProtocolEncoderNiceFlorS {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    const char* nice_flor_s_rainbow_table_file_name;

    uint16_t ic;
    uint32_t cert;
    bool o_code;
    bool cert_on;
};

static const SubGhzBlockConst subghz_protocol_nice_flor_s_const = {
    .te_short = 500,
    .te_long = 1000,
    .te_delta = 300,
    .min_count_bit_for_found = 52,
};

/* host bridge: live firmware functions/state this encoder needs (set by the loader) */
extern bool bypass__bridge_ty;
extern bool nice_flor_s_raw_replay__bridge_ty;
extern uint8_t nice_flors_counter_mode__bridge_ty;
typedef struct {
    void* bypass;
    void* nice_flor_s_raw_replay;
    void* nice_flors_counter_mode;
} enc_nice_flor_s_host_t;
static const void* enc_nice_flor_s_host_raw;
#define ENC_NICE_FLOR_S_HOST ((const enc_nice_flor_s_host_t*)enc_nice_flor_s_host_raw)
#define bypass (*(__typeof__(bypass__bridge_ty)*)(ENC_NICE_FLOR_S_HOST->bypass))
#define nice_flor_s_raw_replay (*(__typeof__(nice_flor_s_raw_replay__bridge_ty)*)(ENC_NICE_FLOR_S_HOST->nice_flor_s_raw_replay))
#define nice_flors_counter_mode (*(__typeof__(nice_flors_counter_mode__bridge_ty)*)(ENC_NICE_FLOR_S_HOST->nice_flors_counter_mode))

static void subghz_protocol_encoder_nice_flor_s_get_upload(
    SubGhzProtocolEncoderNiceFlorS* instance,
    uint8_t btn,
    const char* file_name);
static uint8_t subghz_protocol_nice_flor_s_get_byte_from_buffer(uint8_t* buffer, uint8_t address);
static inline void subghz_protocol_decoder_nice_flor_s_magic_xor(uint8_t* p, uint8_t k);
static uint64_t
    subghz_protocol_nice_flor_s_decrypt(SubGhzBlockGeneric* instance, const char* file_name);
static void subghz_protocol_nice_flor_s_remote_controller(
    SubGhzBlockGeneric* instance,
    const char* file_name,
    uint16_t ic,
    bool o_code);
static uint8_t subghz_protocol_nice_flor_s_get_btn_code(void);

static void subghz_protocol_encoder_nice_flor_s_get_upload(
    SubGhzProtocolEncoderNiceFlorS* instance,
    uint8_t btn,
    const char* file_name) {
    furi_assert(instance);
    size_t index = 0;
    btn = instance->generic.btn;

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(btn);
    }

    btn = subghz_protocol_nice_flor_s_get_btn_code();

    if(subghz_block_generic_global_button_override_get(&btn)) {
        bypass = true;
        FURI_LOG_D(TAG, "Button sucessfully changed to 0x%X", btn);
    }

    size_t size_upload = ((instance->generic.data_count_bit * 2) + ((37 + 2 + 2) * 2) * 16);
    if(size_upload > instance->encoder.size_upload) {
        FURI_LOG_E(TAG, "Size upload exceeds allocated encoder buffer.");
    } else {
        instance->encoder.size_upload = size_upload;
    }

    if(subghz_block_generic_global.cnt_need_override) bypass = true;

    /* Only counter-mode 0 counts; the others pin the field to a literal value a receiver
     * expects, so O-Code runs those on the on-air (swapped) form. */
    const bool cnt_literal =
        instance->o_code &&
        !((nice_flors_counter_mode == 0 || bypass) && !nice_flor_s_raw_replay &&
          (furi_hal_subghz_get_rolling_counter_mult() != -0x7FFFFFFF || bypass));
    if(cnt_literal) instance->generic.cnt = nice_o_cnt_swap(instance->generic.cnt);

    if(nice_flors_counter_mode == 0 || bypass) {
        if(nice_flor_s_raw_replay) {
            bypass = false; /* #16: freeze counter on TxRaw verbatim replay */
        } else if(furi_hal_subghz_get_rolling_counter_mult() != -0x7FFFFFFF || bypass) {
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
    } else if(nice_flors_counter_mode == 1) {
        if(instance->generic.cnt == 0xFFFE) {
            instance->generic.cnt = 0x0001;
        } else {
            instance->generic.cnt = 0xFFFE;
        }
    } else {
        if(instance->generic.cnt >= 0x0001) {
            instance->generic.cnt = 0;
        } else {
            instance->generic.cnt++;
        }
    }

    if(cnt_literal) instance->generic.cnt = nice_o_cnt_swap(instance->generic.cnt);

    const bool o_code = instance->o_code;
    uint8_t buf[SUBGHZ_NICE_FLOR_S_RAINBOW_TABLE_SIZE_BYTES] = {0};
    const bool have_tbl =
        file_name &&
        subghz_keystore_raw_get_data(file_name, 0, buf, SUBGHZ_NICE_FLOR_S_RAINBOW_TABLE_SIZE_BYTES);
    uint64_t decrypt = ((uint64_t)instance->generic.serial << 16) | instance->generic.cnt;
    uint64_t enc_part = have_tbl ? nice_o_encrypt_buf(decrypt, NICE_FLOR_S_IC_PLAIN, buf) :
                                   (uint64_t)SUBGHZ_NO_NICE_FLOR_S_RAINBOW_TABLE;

    /* A certificate sweep is shorter so its longer parcels still fit the buffer. */
    const uint8_t parcels = instance->cert_on ? NICE_O_CERT_PARCELS : 16;

    for(int i = 0; i < parcels; i++) {
        static const uint64_t loops[16] = {
            0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF};

        if(o_code) {
            instance->generic.data = nice_o_make_key(
                instance->generic.serial,
                instance->generic.cnt,
                btn,
                (uint8_t)loops[i],
                instance->ic,
                buf);
        } else {
            uint8_t byte = btn << 4 | (0xF ^ btn ^ loops[i]);
            instance->generic.data = (uint64_t)byte << 44 | enc_part;
        }

        instance->encoder.upload[index++] =
            level_duration_make(false, (uint32_t)subghz_protocol_nice_flor_s_const.te_short * 37);
        instance->encoder.upload[index++] =
            level_duration_make(true, (uint32_t)subghz_protocol_nice_flor_s_const.te_short * 3);
        instance->encoder.upload[index++] =
            level_duration_make(false, (uint32_t)subghz_protocol_nice_flor_s_const.te_short * 3);

        for(uint8_t j = 52; j > 0; j--) {
            if(bit_read(instance->generic.data, j - 1)) {
                instance->encoder.upload[index++] =
                    level_duration_make(true, (uint32_t)subghz_protocol_nice_flor_s_const.te_long);
                instance->encoder.upload[index++] = level_duration_make(
                    false, (uint32_t)subghz_protocol_nice_flor_s_const.te_short);
            } else {
                instance->encoder.upload[index++] = level_duration_make(
                    true, (uint32_t)subghz_protocol_nice_flor_s_const.te_short);
                instance->encoder.upload[index++] = level_duration_make(
                    false, (uint32_t)subghz_protocol_nice_flor_s_const.te_long);
            }
        }
        if(instance->generic.data_count_bit == NICE_ONE_COUNT_BIT) {
            uint8_t add_data[15] = {0};
            for(size_t k = 0; k < 7; k++) {
                add_data[k] = (instance->generic.data >> (48 - k * 8)) & 0xFF;
            }
            /* O-Code marks the last two parcels of every press as release; a captured Nice
             * One instead marks every parcel it sent, so keep that for non-O-Code. */
            uint8_t flags = 0;
            if(o_code) {
                flags = 0x4;
                if(i + NICE_O_RELEASE_PARCELS >= parcels) flags |= 0x1;
            } else if(loops[i]) {
                flags = 0x1;
            }
            if(instance->cert_on) flags |= 0x2;
            nice_o_one_get_data(add_data, (uint8_t)loops[i], flags);
            const size_t tail = instance->cert_on ? 8 : 3;
            if(instance->cert_on) {
                nice_o_cert_data(
                    add_data,
                    (uint8_t)(((loops[i] + 1) ^ instance->generic.cnt) & 0x0F),
                    instance->cert ^ (instance->generic.serial & 0xFFFFFF),
                    instance->generic.data);
            }
            instance->generic.data_2 = 0;
            for(size_t j = 7; j < 7 + tail; j++) {
                instance->generic.data_2 <<= 8;
                instance->generic.data_2 += add_data[j];
            }
            for(uint8_t j = (uint8_t)(tail * 8); j > 4; j--) {
                if(bit_read(instance->generic.data_2, j - 1)) {
                    instance->encoder.upload[index++] = level_duration_make(
                        true, (uint32_t)subghz_protocol_nice_flor_s_const.te_long);
                    instance->encoder.upload[index++] = level_duration_make(
                        false, (uint32_t)subghz_protocol_nice_flor_s_const.te_short);
                } else {
                    instance->encoder.upload[index++] = level_duration_make(
                        true, (uint32_t)subghz_protocol_nice_flor_s_const.te_short);
                    instance->encoder.upload[index++] = level_duration_make(
                        false, (uint32_t)subghz_protocol_nice_flor_s_const.te_long);
                }
            }
        }

        instance->encoder.upload[index++] =
            level_duration_make(true, (uint32_t)subghz_protocol_nice_flor_s_const.te_short * 3);
    }
    instance->encoder.size_upload = index;
}

static uint8_t subghz_protocol_nice_flor_s_get_byte_from_buffer(uint8_t* buffer, uint8_t address) {
    return buffer[address];
}

static inline void subghz_protocol_decoder_nice_flor_s_magic_xor(uint8_t* p, uint8_t k) {
    for(uint8_t i = 1; i < 6; i++) {
        p[i] ^= k;
    }
}

static uint64_t
    subghz_protocol_nice_flor_s_decrypt(SubGhzBlockGeneric* instance, const char* file_name) {
    furi_assert(instance);
    uint64_t data = instance->data;

    if(!file_name) return SUBGHZ_NO_NICE_FLOR_S_RAINBOW_TABLE;

    uint8_t buffer[SUBGHZ_NICE_FLOR_S_RAINBOW_TABLE_SIZE_BYTES] = {0};
    uint8_t* buffer_ptr = (uint8_t*)&buffer;

    if(subghz_keystore_raw_get_data(
           file_name, 0, buffer, SUBGHZ_NICE_FLOR_S_RAINBOW_TABLE_SIZE_BYTES)) {
    } else {
        return SUBGHZ_NO_NICE_FLOR_S_RAINBOW_TABLE;
    }

    uint8_t* p = (uint8_t*)&data;

    uint8_t k = 0;

    k = ~p[4];
    p[5] = ~p[5];
    p[4] = ~p[2];
    p[2] = ~p[0];
    p[0] = k;
    k = ~p[3];
    p[3] = ~p[1];
    p[1] = k;

    for(uint8_t y = 0; y < 2; y++) {
        k = subghz_protocol_nice_flor_s_get_byte_from_buffer(buffer_ptr, p[0] >> 3) + 0x25;
        subghz_protocol_decoder_nice_flor_s_magic_xor(p, k);

        p[5] &= 0x0f;
        p[0] ^= k & 0x7;
        k = subghz_protocol_nice_flor_s_get_byte_from_buffer(buffer_ptr, p[0] & 0x1f);
        subghz_protocol_decoder_nice_flor_s_magic_xor(p, k);

        p[5] &= 0x0f;
        p[0] ^= k & 0xe0;

        if(y == 0) {
            k = p[0];
            p[0] = p[1];
            p[1] = k;
        }
    }

    return data;
}

static void subghz_protocol_nice_flor_s_remote_controller(
    SubGhzBlockGeneric* instance,
    const char* file_name,
    uint16_t ic,
    bool o_code) {
    if(!file_name) {
        instance->cnt = 0;
        instance->serial = 0;
        instance->btn = 0;
    } else if(o_code) {
        uint8_t buf[SUBGHZ_NICE_FLOR_S_RAINBOW_TABLE_SIZE_BYTES] = {0};
        if(subghz_keystore_raw_get_data(
               file_name, 0, buf, SUBGHZ_NICE_FLOR_S_RAINBOW_TABLE_SIZE_BYTES)) {
            uint32_t serial = 0;
            uint16_t cnt = 0;
            uint8_t b = 0;
            nice_o_decode_frame(instance->data, ic, buf, &serial, &cnt, &b);
            instance->serial = serial;
            instance->cnt = cnt;
            instance->btn = b;
        } else {
            instance->cnt = 0;
            instance->serial = 0;
            instance->btn = 0;
        }
    } else {
        uint64_t decrypt = subghz_protocol_nice_flor_s_decrypt(instance, file_name);
        instance->cnt = decrypt & 0xFFFF;
        instance->serial = (decrypt >> 16) & 0xFFFFFFF;
        instance->btn = (decrypt >> 48) & 0xF;
    }

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->btn);
    }
    subghz_custom_btn_set_max(4);
}

static uint8_t subghz_protocol_nice_flor_s_get_btn_code(void) {
    uint8_t custom_btn_id = subghz_custom_btn_get();
    uint8_t original_btn_code = subghz_custom_btn_get_original();
    uint8_t btn = original_btn_code;

    if((custom_btn_id == SUBGHZ_CUSTOM_BTN_OK) && (original_btn_code != 0)) {

        btn = original_btn_code;
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_UP) {
        switch(original_btn_code) {
        case 0x1:
            btn = 0x2;
            break;
        case 0x2:
            btn = 0x1;
            break;
        case 0x4:
            btn = 0x1;
            break;
        case 0x8:
            btn = 0x1;
            break;
        case 0x3:
            btn = 0x1;
            break;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_DOWN) {
        switch(original_btn_code) {
        case 0x1:
            btn = 0x4;
            break;
        case 0x2:
            btn = 0x4;
            break;
        case 0x4:
            btn = 0x2;
            break;
        case 0x8:
            btn = 0x4;
            break;
        case 0x3:
            btn = 0x4;
            break;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_LEFT) {
        switch(original_btn_code) {
        case 0x1:
            btn = 0x8;
            break;
        case 0x2:
            btn = 0x8;
            break;
        case 0x4:
            btn = 0x8;
            break;
        case 0x8:
            btn = 0x2;
            break;
        case 0x3:
            btn = 0x8;
            break;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_RIGHT) {
        switch(original_btn_code) {
        case 0x1:
            btn = 0x3;
            break;
        case 0x2:
            btn = 0x3;
            break;
        case 0x4:
            btn = 0x3;
            break;
        case 0x8:
            btn = 0x3;
            break;
        case 0x3:
            btn = 0x2;
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

void* subghz_protocol_encoder_nice_flor_s_alloc(SubGhzEnvironment* environment) {
    SubGhzProtocolEncoderNiceFlorS* instance = malloc(sizeof(SubGhzProtocolEncoderNiceFlorS));

    instance->base.protocol = &subghz_protocol_nice_flor_s;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->nice_flor_s_rainbow_table_file_name =
        subghz_environment_get_nice_flor_s_rainbow_table_file_name(environment);
    if(instance->nice_flor_s_rainbow_table_file_name) {
        FURI_LOG_D(
            TAG, "Loading rainbow table from %s", instance->nice_flor_s_rainbow_table_file_name);
    }
    instance->encoder.repeat = 1;
    instance->encoder.size_upload = 2400;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_nice_flor_s_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderNiceFlorS* instance = context;
    SubGhzProtocolStatus res = SubGhzProtocolStatusError;
    do {
        /* #16: TxRaw verbatim replay — freeze the rolling counter. */
        nice_flor_s_raw_replay = false;
        {
            uint32_t nice_flor_s_txraw = 0;
            flipper_format_rewind(flipper_format);
            if(flipper_format_read_uint32(flipper_format, "TxRaw", &nice_flor_s_txraw, 1) &&
               nice_flor_s_txraw != 0) {
                nice_flor_s_raw_replay = true;
            }
            flipper_format_rewind(flipper_format);
        }
        if(SubGhzProtocolStatusOk !=
           subghz_block_generic_deserialize(&instance->generic, flipper_format)) {
            FURI_LOG_E(TAG, "Deserialize error");
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
            nice_flors_counter_mode = (uint8_t)tmp_counter_mode;
        } else {
            nice_flors_counter_mode = 0;
        }

        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }
        uint8_t ic_data[2] = {0xFF, 0xFF};
        if(flipper_format_read_hex(flipper_format, "IC", ic_data, sizeof(ic_data))) {
            instance->ic = (uint16_t)ic_data[0] << 8 | ic_data[1];
            instance->o_code = true;
        } else {
            instance->ic = NICE_FLOR_S_IC_PLAIN;
            instance->o_code = false;
        }
        instance->cert = 0;
        instance->cert_on = false;
        if(instance->o_code) {
            uint8_t cert_data[3] = {0};
            if(flipper_format_rewind(flipper_format) &&
               flipper_format_read_hex(flipper_format, "Cert", cert_data, sizeof(cert_data))) {
                instance->cert = (uint32_t)cert_data[0] << 16 | (uint32_t)cert_data[1] << 8 |
                                 cert_data[2];
            }
            uint32_t used = 0;
            flipper_format_rewind(flipper_format);
            flipper_format_read_uint32(flipper_format, "Cert_Cnt", &used, 1);
            instance->cert_on = instance->cert && (used < NICE_O_CERT_SENDS) &&
                                (instance->generic.data_count_bit == NICE_ONE_COUNT_BIT);
        }
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }

        subghz_protocol_nice_flor_s_remote_controller(
            &instance->generic, instance->nice_flor_s_rainbow_table_file_name, instance->ic,
            instance->o_code);
        subghz_protocol_encoder_nice_flor_s_get_upload(
            instance, instance->generic.btn, instance->nice_flor_s_rainbow_table_file_name);

        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }
        uint8_t key_data[sizeof(uint64_t)] = {0};
        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            key_data[sizeof(uint64_t) - i - 1] = (instance->generic.data >> i * 8) & 0xFF;
        }
        if(!flipper_format_update_hex(flipper_format, "Key", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Unable to update Key");
            break;
        }

        if(instance->generic.data_count_bit == NICE_ONE_COUNT_BIT) {
            if(!flipper_format_rewind(flipper_format)) {
                FURI_LOG_E(TAG, "Rewind error");
                break;
            }
            uint32_t temp = (instance->generic.data_2 >> 4) & 0xFFFFF;
            if(!flipper_format_update_uint32(flipper_format, "Data", &temp, 1)) {
                FURI_LOG_E(TAG, "Unable to update Data");
            }
        }

        instance->encoder.is_running = true;

        res = SubGhzProtocolStatusOk;
    } while(false);

    return res;
}

uint64_t subghz_protocol_nice_flor_s_encrypt(uint64_t data, const char* file_name) {

    if(!file_name) return SUBGHZ_NO_NICE_FLOR_S_RAINBOW_TABLE;

    uint8_t buffer[SUBGHZ_NICE_FLOR_S_RAINBOW_TABLE_SIZE_BYTES] = {0};
    uint8_t* buffer_ptr = (uint8_t*)&buffer;

    if(subghz_keystore_raw_get_data(
           file_name, 0, buffer, SUBGHZ_NICE_FLOR_S_RAINBOW_TABLE_SIZE_BYTES)) {
    } else {
        return SUBGHZ_NO_NICE_FLOR_S_RAINBOW_TABLE;
    }

    uint8_t* p = (uint8_t*)&data;

    uint8_t k = 0;
    for(uint8_t y = 0; y < 2; y++) {
        k = subghz_protocol_nice_flor_s_get_byte_from_buffer(buffer_ptr, p[0] & 0x1f);
        subghz_protocol_decoder_nice_flor_s_magic_xor(p, k);

        p[5] &= 0x0f;
        p[0] ^= k & 0xe0;
        k = subghz_protocol_nice_flor_s_get_byte_from_buffer(buffer_ptr, p[0] >> 3) + 0x25;
        subghz_protocol_decoder_nice_flor_s_magic_xor(p, k);

        p[5] &= 0x0f;
        p[0] ^= k & 0x7;
        if(y == 0) {
            k = p[0];
            p[0] = p[1];
            p[1] = k;
        }
    }

    p[5] = ~p[5] & 0x0f;
    k = ~p[4];
    p[4] = ~p[0];
    p[0] = ~p[2];
    p[2] = k;
    k = ~p[3];
    p[3] = ~p[1];
    p[1] = k;

    return data;
}

static const SubGhzProtocolEncoder enc_nice_flor_s_vt = {
    .alloc = subghz_protocol_encoder_nice_flor_s_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_nice_flor_s_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_nice_flor_s = {
    .name = SUBGHZ_PROTOCOL_NICE_FLOR_S_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay | SubGhzProtocolFlag_NiceFlorS,
    .decoder = NULL,
    .encoder = &enc_nice_flor_s_vt,
};

static const SubGhzEncodePluginEntry enc_nice_flor_s_entry = {.name = SUBGHZ_PROTOCOL_NICE_FLOR_S_NAME, .encoder = &enc_nice_flor_s_vt, .host_slot = &enc_nice_flor_s_host_raw};
static const FlipperAppPluginDescriptor enc_nice_flor_s_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_nice_flor_s_entry};
const FlipperAppPluginDescriptor* enc_nice_flor_s_ep(void) { return &enc_nice_flor_s_descriptor; }
