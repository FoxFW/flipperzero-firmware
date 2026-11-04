/* enc_faac_slh.fal : SD encoder for faac_slh (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_stop enc_faac_slh__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_faac_slh__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_faac_slh_alloc enc_faac_slh__subghz_protocol_encoder_faac_slh_alloc
#define subghz_protocol_encoder_faac_slh_deserialize enc_faac_slh__subghz_protocol_encoder_faac_slh_deserialize
#define subghz_protocol_encoder_faac_slh_free enc_faac_slh__subghz_protocol_encoder_faac_slh_free
#define subghz_protocol_faac_slh enc_faac_slh__subghz_protocol_faac_slh
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
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
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolFaacSLH"

struct SubGhzProtocolEncoderFaacSLH {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    SubGhzKeystore* keystore;
    const char* manufacture_name;

    FuriString* manufacture_from_file;
};

static const SubGhzBlockConst subghz_protocol_faac_slh_const = {
    .te_short = 255,
    .te_long = 595,
    .te_delta = 100,
    .min_count_bit_for_found = 64,
};

/* host bridge: live firmware functions/state this encoder needs (set by the loader) */
extern bool allow_zero_seed__bridge_ty;
extern bool faac_prog_mode__bridge_ty;
extern bool faac_slh_raw_replay__bridge_ty;
extern uint32_t temp_counter_backup__bridge_ty;
extern uint32_t temp_fix_backup__bridge_ty;
typedef struct {
    void* allow_zero_seed;
    void* faac_prog_mode;
    void* faac_slh_raw_replay;
    __typeof__(&subghz_protocol_keeloq_common_decrypt) subghz_protocol_keeloq_common_decrypt;
    __typeof__(&subghz_protocol_keeloq_common_encrypt) subghz_protocol_keeloq_common_encrypt;
    __typeof__(&subghz_protocol_keeloq_common_faac_learning) subghz_protocol_keeloq_common_faac_learning;
    void* temp_counter_backup;
    void* temp_fix_backup;
} enc_faac_slh_host_t;
static const void* enc_faac_slh_host_raw;
#define ENC_FAAC_SLH_HOST ((const enc_faac_slh_host_t*)enc_faac_slh_host_raw)
#define allow_zero_seed (*(__typeof__(allow_zero_seed__bridge_ty)*)(ENC_FAAC_SLH_HOST->allow_zero_seed))
#define faac_prog_mode (*(__typeof__(faac_prog_mode__bridge_ty)*)(ENC_FAAC_SLH_HOST->faac_prog_mode))
#define faac_slh_raw_replay (*(__typeof__(faac_slh_raw_replay__bridge_ty)*)(ENC_FAAC_SLH_HOST->faac_slh_raw_replay))
#define subghz_protocol_keeloq_common_decrypt (ENC_FAAC_SLH_HOST->subghz_protocol_keeloq_common_decrypt)
#define subghz_protocol_keeloq_common_encrypt (ENC_FAAC_SLH_HOST->subghz_protocol_keeloq_common_encrypt)
#define subghz_protocol_keeloq_common_faac_learning (ENC_FAAC_SLH_HOST->subghz_protocol_keeloq_common_faac_learning)
#define temp_counter_backup (*(__typeof__(temp_counter_backup__bridge_ty)*)(ENC_FAAC_SLH_HOST->temp_counter_backup))
#define temp_fix_backup (*(__typeof__(temp_fix_backup__bridge_ty)*)(ENC_FAAC_SLH_HOST->temp_fix_backup))

/**
 * Select the manufacture key for a Faac SLH signal (Genius mechanism, ported from
 * Unleashed 2026-10-03). Several manufacturers share this protocol and differ only
 * in the type-5 key the cipher is seeded with (e.g. FAAC_SLH and Genius), so the
 * .sub file names the one it belongs to via a "Manufacture:" field. No name means a
 * plain Faac remote (what every file written before Genius existed relies on), so
 * the default is "FAAC_SLH". Falls back to FAAC_SLH if the keystore lacks the named
 * entry. Returns 0 if the keystore has no Faac SLH entry at all.
 *
 * This replaces the previous last-wins loop, which meant only one type-5 key could
 * live in a keystore at a time (why "Genius" had to be dropped from the garage
 * keystore). Now multiple type-5 keys coexist and the file picks the right one.
 */
static uint64_t subghz_protocol_faac_slh_get_key(
    SubGhzKeystore* keystore,
    const char* wanted,
    const char** manufacture_name);
static bool subghz_protocol_faac_slh_gen_data(SubGhzProtocolEncoderFaacSLH* instance);
static bool subghz_protocol_encoder_faac_slh_get_upload(SubGhzProtocolEncoderFaacSLH* instance);
static void subghz_protocol_faac_slh_check_remote_controller(
    SubGhzBlockGeneric* instance,
    SubGhzKeystore* keystore,
    const char** manufacture_name,
    const char* manufacture_from_file);

/**
 * Select the manufacture key for a Faac SLH signal (Genius mechanism, ported from
 * Unleashed 2026-10-03). Several manufacturers share this protocol and differ only
 * in the type-5 key the cipher is seeded with (e.g. FAAC_SLH and Genius), so the
 * .sub file names the one it belongs to via a "Manufacture:" field. No name means a
 * plain Faac remote (what every file written before Genius existed relies on), so
 * the default is "FAAC_SLH". Falls back to FAAC_SLH if the keystore lacks the named
 * entry. Returns 0 if the keystore has no Faac SLH entry at all.
 *
 * This replaces the previous last-wins loop, which meant only one type-5 key could
 * live in a keystore at a time (why "Genius" had to be dropped from the garage
 * keystore). Now multiple type-5 keys coexist and the file picks the right one.
 */
static uint64_t subghz_protocol_faac_slh_get_key(
    SubGhzKeystore* keystore,
    const char* wanted,
    const char** manufacture_name) {
    uint64_t faac_key = 0;
    const char* faac_name = NULL;
    if(!wanted || (*wanted == '\0')) wanted = "FAAC_SLH";

    SUBGHZ_KEYSTORE_FOR_EACH(manufacture_code, keystore) {
            if(manufacture_code->type != KEELOQ_LEARNING_FAAC) continue;
            const char* name = furi_string_get_cstr(manufacture_code->name);
            if(strcmp(name, wanted) == 0) {
                *manufacture_name = subghz_keystore_keep_name(keystore, name);
                return manufacture_code->key;
            }
            // Remember FAAC_SLH so we can fall back when the file names something
            // this keystore does not carry.
            if(strcmp(name, "FAAC_SLH") == 0) {
                faac_key = manufacture_code->key;
                faac_name = subghz_keystore_keep_name(keystore, name);
            }
        }
    if(faac_name) *manufacture_name = faac_name;
    return faac_key;
}

static bool subghz_protocol_faac_slh_gen_data(SubGhzProtocolEncoderFaacSLH* instance) {

    if(subghz_block_generic_global_button_override_get(&instance->generic.btn)) {
        FURI_LOG_D(TAG, "Button sucessfully changed to 0x%X", instance->generic.btn);
    } else {

        if(subghz_custom_btn_get_original() == 0) {
            subghz_custom_btn_set_original(0xF);
        }

        uint8_t custom_btn_id = subghz_custom_btn_get();

        if((custom_btn_id == SUBGHZ_CUSTOM_BTN_UP) &&
           !(!allow_zero_seed && (instance->generic.seed == 0x0))) {
            uint8_t data_tmp = 0;
            uint8_t data_prg[8];

            data_prg[0] = 0x00;

            if(allow_zero_seed || (instance->generic.seed != 0x0)) {

                if(faac_slh_raw_replay) {
                    /* #16: freeze counter on TxRaw verbatim replay */
                } else if(furi_hal_subghz_get_rolling_counter_mult() != -0x7FFFFFFF) {

                    if(!subghz_block_generic_global_counter_override_get(&instance->generic.cnt)) {

                        if((instance->generic.cnt + furi_hal_subghz_get_rolling_counter_mult()) >
                           0xFFFFF) {
                            instance->generic.cnt = 0;
                        } else {
                            instance->generic.cnt += furi_hal_subghz_get_rolling_counter_mult();
                        }
                    }
                } else {

                    instance->generic.cnt += 1;
                }

                if(temp_counter_backup != 0x0) {

                    if(faac_slh_raw_replay) {
                        /* #16: freeze counter on TxRaw verbatim replay */
                    } else if(furi_hal_subghz_get_rolling_counter_mult() != -0x7FFFFFFF) {

                        if(!subghz_block_generic_global_counter_override_get(
                               &temp_counter_backup)) {

                            if((temp_counter_backup + furi_hal_subghz_get_rolling_counter_mult()) >
                               0xFFFFF) {
                                temp_counter_backup = 0;
                            } else {
                                temp_counter_backup += furi_hal_subghz_get_rolling_counter_mult();
                            }
                        }
                    } else {

                        temp_counter_backup += 1;
                    }
                }
            }

            data_prg[1] = instance->generic.cnt & 0xFF;

            data_prg[2] = (uint8_t)(instance->generic.seed & 0xFF);
            data_prg[3] = (uint8_t)(instance->generic.seed >> 8 & 0xFF);
            data_prg[4] = (uint8_t)(instance->generic.seed >> 16 & 0xFF);
            data_prg[5] = (uint8_t)(instance->generic.seed >> 24);

            data_prg[2] ^= data_prg[1];
            data_prg[3] ^= data_prg[1];
            data_prg[4] ^= data_prg[1];
            data_prg[5] ^= data_prg[1];

            for(uint8_t i = data_prg[1] & 0x0F; i != 0; i--) {
                data_tmp = data_prg[5];

                data_prg[5] = ((data_prg[5] << 1) & 0xFF) | (data_prg[4] & 0x80) >> 7;
                data_prg[4] = ((data_prg[4] << 1) & 0xFF) | (data_prg[3] & 0x80) >> 7;
                data_prg[3] = ((data_prg[3] << 1) & 0xFF) | (data_prg[2] & 0x80) >> 7;
                data_prg[2] = ((data_prg[2] << 1) & 0xFF) | (data_tmp & 0x80) >> 7;
            }
            data_prg[6] = 0x0F;
            data_prg[7] = 0x52;

            uint32_t enc_prg_1 = data_prg[7] << 24 | data_prg[6] << 16 | data_prg[5] << 8 |
                                 data_prg[4];
            uint32_t enc_prg_2 = data_prg[3] << 24 | data_prg[2] << 16 | data_prg[1] << 8 |
                                 data_prg[0];
            instance->generic.data = (uint64_t)enc_prg_1 << 32 | enc_prg_2;

            return true;
        } else {
            if(!allow_zero_seed && (instance->generic.seed == 0x0)) {

                return true;
            }

            if((faac_prog_mode == true) && (instance->generic.serial == 0x0) &&
               (instance->generic.btn == 0x0) && (temp_fix_backup == 0x0)) {
                return true;
            }
        }

        if((instance->generic.serial == 0x0) && (instance->generic.btn == 0x0) &&
           (temp_fix_backup != 0x0) && !faac_prog_mode) {
            instance->generic.serial = temp_fix_backup >> 4;
            instance->generic.btn = temp_fix_backup & 0xF;
            if(temp_counter_backup != 0x0) {
                instance->generic.cnt = temp_counter_backup;
            }
        }
    }
    uint32_t fix = instance->generic.serial << 4 | instance->generic.btn;
    uint32_t hop = 0;
    uint32_t decrypt = 0;
    uint64_t man = 0;
    char fixx[8] = {};
    int shiftby = 32;
    for(int i = 0; i < 8; i++) {
        fixx[i] = (fix >> (shiftby -= 4)) & 0xF;
    }

    if(allow_zero_seed || (instance->generic.seed != 0x0)) {

        if(faac_slh_raw_replay) {
            /* #16: freeze counter on TxRaw verbatim replay */
        } else if(furi_hal_subghz_get_rolling_counter_mult() != -0x7FFFFFFF) {

            if(!subghz_block_generic_global_counter_override_get(&instance->generic.cnt)) {

                if((instance->generic.cnt + furi_hal_subghz_get_rolling_counter_mult()) >
                   0xFFFFF) {
                    instance->generic.cnt = 0;
                } else {
                    instance->generic.cnt += furi_hal_subghz_get_rolling_counter_mult();
                }
            }
        } else {

            if(instance->generic.cnt < 0xFFFFF) {
                if((instance->generic.cnt + 0xFFFFF) > 0xFFFFF) {
                    instance->generic.cnt = 0;
                } else {
                    instance->generic.cnt += 0xFFFFF;
                }
            } else if(
                (instance->generic.cnt >= 0xFFFFF) &&
                (furi_hal_subghz_get_rolling_counter_mult() != 0)) {
                instance->generic.cnt = 0;
            }
        }
    }

    if((instance->generic.cnt % 2) == 0) {
        decrypt = fixx[6] << 28 | fixx[7] << 24 | fixx[5] << 20 |
                  (instance->generic.cnt & 0xFFFFF);
    } else {
        decrypt = fixx[2] << 28 | fixx[3] << 24 | fixx[4] << 20 |
                  (instance->generic.cnt & 0xFFFFF);
    }
    // Transmit with the manufacturer the file named, FAAC_SLH otherwise (Genius
    // mechanism). Re-pin manufacture_name to the entry actually used.
    const char* used = NULL;
    uint64_t key = subghz_protocol_faac_slh_get_key(
        instance->keystore, instance->manufacture_name, &used);
    if(used) instance->manufacture_name = used;
    if(key) {
        // FAAC Learning
        man = subghz_protocol_keeloq_common_faac_learning(instance->generic.seed, key);
        hop = subghz_protocol_keeloq_common_encrypt(decrypt, man);
    }
    if(hop) {
        instance->generic.data = (uint64_t)fix << 32 | hop;
    }
    return true;
}

static bool subghz_protocol_encoder_faac_slh_get_upload(SubGhzProtocolEncoderFaacSLH* instance) {
    furi_assert(instance);

    subghz_protocol_faac_slh_gen_data(instance);
    size_t index = 0;
    size_t size_upload = 2 + (instance->generic.data_count_bit * 2);
    if(size_upload > instance->encoder.size_upload) {
        FURI_LOG_E(TAG, "Size upload exceeds allocated encoder buffer.");
        return false;
    } else {
        instance->encoder.size_upload = size_upload;
    }

    instance->encoder.upload[index++] =
        level_duration_make(true, (uint32_t)subghz_protocol_faac_slh_const.te_long * 2);
    instance->encoder.upload[index++] =
        level_duration_make(false, (uint32_t)subghz_protocol_faac_slh_const.te_long * 2);

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if(bit_read(instance->generic.data, i - 1)) {

            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_faac_slh_const.te_long);
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_faac_slh_const.te_short);
        } else {

            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_faac_slh_const.te_short);
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_faac_slh_const.te_long);
        }
    }
    return true;
}

static void subghz_protocol_faac_slh_check_remote_controller(
    SubGhzBlockGeneric* instance,
    SubGhzKeystore* keystore,
    const char** manufacture_name,
    const char* manufacture_from_file) {
    uint32_t code_fix = instance->data >> 32;
    uint32_t code_hop = instance->data & 0xFFFFFFFF;
    uint32_t decrypt = 0;

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(0xF);
    }

    subghz_custom_btn_set_max(1);

    uint8_t data_tmp = 0;
    uint8_t data_prg[8];
    data_prg[0] = (code_hop & 0xFF);
    data_prg[1] = ((code_hop >> 8) & 0xFF);
    data_prg[2] = ((code_hop >> 16) & 0xFF);
    data_prg[3] = (code_hop >> 24);
    data_prg[4] = (code_fix & 0xFF);
    data_prg[5] = ((code_fix >> 8) & 0xFF);
    data_prg[6] = ((code_fix >> 16) & 0xFF);
    data_prg[7] = (code_fix >> 24);

    if(((data_prg[7] == 0x52) && (data_prg[6] == 0x0F) && (data_prg[0] == 0x00))) {
        faac_prog_mode = true;

        for(uint8_t i = data_prg[1] & 0xF; i != 0; i--) {
            data_tmp = data_prg[2];

            data_prg[2] = data_prg[2] >> 1 | (data_prg[3] & 1) << 7;
            data_prg[3] = data_prg[3] >> 1 | (data_prg[4] & 1) << 7;
            data_prg[4] = data_prg[4] >> 1 | (data_prg[5] & 1) << 7;
            data_prg[5] = data_prg[5] >> 1 | (data_tmp & 1) << 7;
        }
        data_prg[2] ^= data_prg[1];
        data_prg[3] ^= data_prg[1];
        data_prg[4] ^= data_prg[1];
        data_prg[5] ^= data_prg[1];
        instance->seed = data_prg[5] << 24 | data_prg[4] << 16 | data_prg[3] << 8 | data_prg[2];
        uint32_t dec_prg_1 = data_prg[7] << 24 | data_prg[6] << 16 | data_prg[5] << 8 |
                             data_prg[4];
        uint32_t dec_prg_2 = data_prg[3] << 24 | data_prg[2] << 16 | data_prg[1] << 8 |
                             data_prg[0];
        instance->data_2 = (uint64_t)dec_prg_1 << 32 | dec_prg_2;
        instance->cnt = data_prg[1];

        *manufacture_name = "FAAC_SLH";
        return;
    } else {
        if(code_fix != 0x0) {
            temp_fix_backup = code_fix;
            instance->serial = code_fix >> 4;
            instance->btn = code_fix & 0xF;
        }

        faac_prog_mode = false;
    }

    // Select the type-5 key by the manufacturer the file named (Genius mechanism),
    // FAAC_SLH otherwise, instead of last-wins across the keystore.
    uint64_t key =
        subghz_protocol_faac_slh_get_key(keystore, manufacture_from_file, manufacture_name);
    if(key) {
        // FAAC Learning
        uint64_t man = subghz_protocol_keeloq_common_faac_learning(instance->seed, key);
        decrypt = subghz_protocol_keeloq_common_decrypt(code_hop, man);
    }
    instance->cnt = decrypt & 0xFFFFF;

    if(code_fix != 0x0) {
        temp_counter_backup = instance->cnt;
    }
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

void* subghz_protocol_encoder_faac_slh_alloc(SubGhzEnvironment* environment) {
    SubGhzProtocolEncoderFaacSLH* instance = malloc(sizeof(SubGhzProtocolEncoderFaacSLH));

    instance->base.protocol = &subghz_protocol_faac_slh;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->keystore = subghz_environment_get_keystore(environment);
    instance->manufacture_from_file = furi_string_alloc();

    instance->encoder.repeat = 3;
    instance->encoder.size_upload = 256;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_faac_slh_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderFaacSLH* instance = context;
    furi_string_free(instance->manufacture_from_file);
    free(instance->encoder.upload);
    free(instance);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_faac_slh_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderFaacSLH* instance = context;
    SubGhzProtocolStatus res = SubGhzProtocolStatusError;
    do {
        /* #16: TxRaw verbatim replay — freeze the rolling counter. */
        faac_slh_raw_replay = false;
        {
            uint32_t faac_slh_txraw = 0;
            flipper_format_rewind(flipper_format);
            if(flipper_format_read_uint32(flipper_format, "TxRaw", &faac_slh_txraw, 1) &&
               faac_slh_txraw != 0) {
                faac_slh_raw_replay = true;
            }
            flipper_format_rewind(flipper_format);
        }
        if(SubGhzProtocolStatusOk !=
           subghz_block_generic_deserialize(&instance->generic, flipper_format)) {
            FURI_LOG_E(TAG, "Deserialize error");
            break;
        }
        uint8_t seed_data[sizeof(uint32_t)] = {0};
        for(size_t i = 0; i < sizeof(uint32_t); i++) {
            seed_data[sizeof(uint32_t) - i - 1] = (instance->generic.seed >> i * 8) & 0xFF;
        }
        if(!flipper_format_read_hex(flipper_format, "Seed", seed_data, sizeof(uint32_t))) {
            FURI_LOG_E(TAG, "Missing Seed");
            break;
        }
        bool tmp_allow_zero_seed;
        if(flipper_format_read_bool(flipper_format, "AllowZeroSeed", &tmp_allow_zero_seed, 1)) {
            allow_zero_seed = true;
        } else {
            allow_zero_seed = false;
        }

        instance->generic.seed = seed_data[0] << 24 | seed_data[1] << 16 | seed_data[2] << 8 |
                                 seed_data[3];

        // Optional. Pins the signal to one of the FAAC SLH manufacturers; without it
        // the manufacturer is recovered from the signal. Read from the top of the file
        // so the key may sit anywhere, and rewind after so later reads still see
        // everything. (Genius mechanism, ported from Unleashed 2026-10-03.)
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }
        if(!flipper_format_read_string(
               flipper_format, "Manufacture", instance->manufacture_from_file)) {
            furi_string_reset(instance->manufacture_from_file);
        }
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }

        subghz_protocol_faac_slh_check_remote_controller(
            &instance->generic,
            instance->keystore,
            &instance->manufacture_name,
            furi_string_get_cstr(instance->manufacture_from_file));

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        subghz_protocol_encoder_faac_slh_get_upload(instance);

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

        instance->encoder.is_running = true;

        res = SubGhzProtocolStatusOk;
    } while(false);

    return res;
}

static const SubGhzProtocolEncoder enc_faac_slh_vt = {
    .alloc = subghz_protocol_encoder_faac_slh_alloc,
    .free = subghz_protocol_encoder_faac_slh_free,
    .deserialize = subghz_protocol_encoder_faac_slh_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_faac_slh = {
    .name = SUBGHZ_PROTOCOL_FAAC_SLH_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = NULL,
    .encoder = &enc_faac_slh_vt,
};

static const SubGhzEncodePluginEntry enc_faac_slh_entry = {.name = SUBGHZ_PROTOCOL_FAAC_SLH_NAME, .encoder = &enc_faac_slh_vt, .host_slot = &enc_faac_slh_host_raw};
static const FlipperAppPluginDescriptor enc_faac_slh_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_faac_slh_entry};
const FlipperAppPluginDescriptor* enc_faac_slh_ep(void) { return &enc_faac_slh_descriptor; }
