#include "faac_slh.h"

#include "common.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"
#include "../subghz_keystore.h"
#include <m-array.h>
#include "keeloq_common.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#include "../blocks/custom_btn_i.h"

#define TAG "SubGhzProtocolFaacSLH"

static bool faac_slh_raw_replay = false; // #16: TxRaw verbatim replay (freeze counter)

static const SubGhzBlockConst subghz_protocol_faac_slh_const = {
    .te_short = 255,
    .te_long = 595,
    .te_delta = 100,
    .min_count_bit_for_found = 64,
};

static uint32_t temp_fix_backup = 0;
static uint32_t temp_counter_backup = 0;
static bool faac_prog_mode = false;
static bool allow_zero_seed = false;

void faac_slh_reset_prog_mode(void) {
    temp_fix_backup = 0;
    temp_counter_backup = 0;
    faac_prog_mode = false;
    allow_zero_seed = false;
}

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

struct SubGhzProtocolEncoderFaacSLH {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    SubGhzKeystore* keystore;
    const char* manufacture_name;

    FuriString* manufacture_from_file;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderFaacSLH);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderFaacSLH);

typedef enum {
    FaacSLHDecoderStepReset = 0,
    FaacSLHDecoderStepFoundPreambula,
    FaacSLHDecoderStepSaveDuration,
    FaacSLHDecoderStepCheckDuration,
} FaacSLHDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_faac_slh_decoder = {
    .alloc = subghz_protocol_decoder_faac_slh_alloc,
    .free = subghz_protocol_decoder_faac_slh_free,

    .feed = subghz_protocol_decoder_faac_slh_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_faac_slh_serialize,
    .deserialize = subghz_protocol_decoder_faac_slh_deserialize,
    .get_string = subghz_protocol_decoder_faac_slh_get_string,
};

/* host bridge: firmware functions/state the SD encoder uses (handed over at load) */
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
static const enc_faac_slh_host_t enc_faac_slh_host = {
    .allow_zero_seed = (void*)&allow_zero_seed,
    .faac_prog_mode = (void*)&faac_prog_mode,
    .faac_slh_raw_replay = (void*)&faac_slh_raw_replay,
    .subghz_protocol_keeloq_common_decrypt = subghz_protocol_keeloq_common_decrypt,
    .subghz_protocol_keeloq_common_encrypt = subghz_protocol_keeloq_common_encrypt,
    .subghz_protocol_keeloq_common_faac_learning = subghz_protocol_keeloq_common_faac_learning,
    .temp_counter_backup = (void*)&temp_counter_backup,
    .temp_fix_backup = (void*)&temp_fix_backup,
};
static const SubGhzEncoderExternal subghz_protocol_faac_slh_encoder_ext = SUBGHZ_ENCODER_EXTERNAL_HOST("enc_faac_slh", &enc_faac_slh_host);

const SubGhzProtocol subghz_protocol_faac_slh = {
    .name = SUBGHZ_PROTOCOL_FAAC_SLH_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,

    .decoder = &subghz_protocol_faac_slh_decoder,
    .encoder = &subghz_protocol_faac_slh_encoder_ext.vtable,
};

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

bool subghz_protocol_faac_slh_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint32_t cnt,
    uint32_t seed,
    const char* manufacture_name,
    SubGhzRadioPreset* preset) {
    furi_assert(context);

    SubGhzProtocolEncoderFaacSLH* instance = context;
    instance->generic.serial = serial;
    instance->generic.btn = btn;
    instance->generic.cnt = (cnt & 0xFFFFF);
    instance->generic.seed = seed;
    instance->manufacture_name = manufacture_name;
    instance->generic.data_count_bit = 64;
    allow_zero_seed = true;
    bool res = subghz_protocol_faac_slh_gen_data(instance);
    if(res) {
        return SubGhzProtocolStatusOk ==
               subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    }
    return res;
}

void* subghz_protocol_decoder_faac_slh_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderFaacSLH* instance = malloc(sizeof(SubGhzProtocolDecoderFaacSLH));
    instance->base.protocol = &subghz_protocol_faac_slh;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->keystore = subghz_environment_get_keystore(environment);
    instance->manufacture_from_file = furi_string_alloc();
    return instance;
}

void subghz_protocol_decoder_faac_slh_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderFaacSLH* instance = context;
    furi_string_free(instance->manufacture_from_file);
    free(instance);
}

void subghz_protocol_decoder_faac_slh_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderFaacSLH* instance = context;

    switch(instance->decoder.parser_step) {
    case FaacSLHDecoderStepReset:
        if((level) && (DURATION_DIFF(duration, subghz_protocol_faac_slh_const.te_long * 2) <
                       subghz_protocol_faac_slh_const.te_delta * 3)) {
            instance->decoder.parser_step = FaacSLHDecoderStepFoundPreambula;
        }
        break;
    case FaacSLHDecoderStepFoundPreambula:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_faac_slh_const.te_long * 2) <
                        subghz_protocol_faac_slh_const.te_delta * 3)) {

            instance->decoder.parser_step = FaacSLHDecoderStepSaveDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
        } else {
            instance->decoder.parser_step = FaacSLHDecoderStepReset;
        }
        break;
    case FaacSLHDecoderStepSaveDuration:
        if(level) {
            if(duration >= ((uint32_t)subghz_protocol_faac_slh_const.te_short * 3 +
                            subghz_protocol_faac_slh_const.te_delta)) {
                instance->decoder.parser_step = FaacSLHDecoderStepFoundPreambula;
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_faac_slh_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                break;
            } else {
                instance->decoder.te_last = duration;
                instance->decoder.parser_step = FaacSLHDecoderStepCheckDuration;
            }

        } else {
            instance->decoder.parser_step = FaacSLHDecoderStepReset;
        }
        break;
    case FaacSLHDecoderStepCheckDuration:
        if(!level) {
            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_faac_slh_const.te_short) <
                subghz_protocol_faac_slh_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_faac_slh_const.te_long) <
                subghz_protocol_faac_slh_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = FaacSLHDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_faac_slh_const.te_long) <
                 subghz_protocol_faac_slh_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_faac_slh_const.te_short) <
                 subghz_protocol_faac_slh_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = FaacSLHDecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = FaacSLHDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = FaacSLHDecoderStepReset;
        }
        break;
    }
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

SubGhzProtocolStatus subghz_protocol_decoder_faac_slh_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderFaacSLH* instance = context;

    instance->generic.seed = 0x0;
    temp_fix_backup = 0x0;

    SubGhzProtocolStatus res =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    uint8_t seed_data[sizeof(uint32_t)] = {0};
    for(size_t i = 0; i < sizeof(uint32_t); i++) {
        seed_data[sizeof(uint32_t) - i - 1] = (instance->generic.seed >> i * 8) & 0xFF;
    }
    if((res == SubGhzProtocolStatusOk) &&
       !flipper_format_write_hex(flipper_format, "Seed", seed_data, sizeof(uint32_t))) {
        FURI_LOG_E(TAG, "Unable to add Seed");
        res = SubGhzProtocolStatusError;
    }
    instance->generic.seed = seed_data[0] << 24 | seed_data[1] << 16 | seed_data[2] << 8 |
                             seed_data[3];

    subghz_protocol_faac_slh_check_remote_controller(
        &instance->generic,
        instance->keystore,
        &instance->manufacture_name,
        furi_string_get_cstr(instance->manufacture_from_file));

    // Only carry a manufacturer forward when the file already had one. The seed is
    // cleared above, so nothing can be verified here and guessing would pin the signal
    // to the wrong entry for good. (Genius mechanism, ported from Unleashed.)
    if((res == SubGhzProtocolStatusOk) && !furi_string_empty(instance->manufacture_from_file) &&
       !flipper_format_write_string_cstr(
           flipper_format, "Manufacture", furi_string_get_cstr(instance->manufacture_from_file))) {
        FURI_LOG_E(TAG, "Unable to add Manufacture");
        res = SubGhzProtocolStatusError;
    }

    return res;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_faac_slh_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderFaacSLH* instance = context;
    SubGhzProtocolStatus res = SubGhzProtocolStatusError;
    do {
        if(SubGhzProtocolStatusOk !=
           subghz_block_generic_deserialize(&instance->generic, flipper_format)) {
            FURI_LOG_E(TAG, "Deserialize error");
            break;
        }
        if(instance->generic.data_count_bit !=
           subghz_protocol_faac_slh_const.min_count_bit_for_found) {
            FURI_LOG_E(TAG, "Wrong number of bits in key");
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

        // Optional "Manufacture:" field pins the signal to one FAAC SLH manufacturer
        // (e.g. Genius) so get_string decodes with the right type-5 key. Read from the
        // top, then rewind. (Genius mechanism, ported from Unleashed 2026-10-03.)
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
        res = SubGhzProtocolStatusOk;
    } while(false);

    return res;
}

void subghz_protocol_decoder_faac_slh_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderFaacSLH* instance = context;
    // Detect/decode split (FoxFW2.0 v2.1): KeeLoq/FAAC derivation needs the live
    // keystore and sets the prog-mode state, so it stays internal; the state is
    // read after it and handed to the SD plugin, which only formats.
    subghz_protocol_faac_slh_check_remote_controller(
        &instance->generic,
        instance->keystore,
        &instance->manufacture_name,
        furi_string_get_cstr(instance->manufacture_from_file));
    const uint64_t extra[2] = {(uint64_t)faac_prog_mode, (uint64_t)allow_zero_seed};
    const SubGhzDecodeInput input = {
        .decoder = context,
        .generic = &instance->generic,
        .extra = extra,
        .extra_count = 2,
        .aux = NULL,
    };
    subghz_decode_plugin_get_string(instance->generic.protocol_name, &input, output);
}

