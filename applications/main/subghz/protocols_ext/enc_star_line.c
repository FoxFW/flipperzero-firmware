/* enc_star_line.fal : SD encoder for star_line (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_star_line_alloc enc_star_line__subghz_protocol_encoder_star_line_alloc
#define subghz_protocol_encoder_star_line_deserialize enc_star_line__subghz_protocol_encoder_star_line_deserialize
#define subghz_protocol_encoder_star_line_free enc_star_line__subghz_protocol_encoder_star_line_free
#define subghz_protocol_encoder_star_line_stop enc_star_line__subghz_protocol_encoder_star_line_stop
#define subghz_protocol_encoder_star_line_yield enc_star_line__subghz_protocol_encoder_star_line_yield
#define subghz_protocol_star_line enc_star_line__subghz_protocol_star_line
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/star_line.h>
#include <lib/subghz/protocols/keeloq_common.h>
#include <lib/subghz/subghz_keystore.h>
#include <lib/subghz/subghz_keystore_i.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolStarLine"

struct SubGhzProtocolEncoderStarLine {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    SubGhzKeystore* keystore;
    const char* manufacture_name;

    FuriString* manufacture_from_file;
};

static const SubGhzBlockConst subghz_protocol_star_line_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 120,
    .min_count_bit_for_found = 64,
};

/* host bridge: live firmware functions/state this encoder needs (set by the loader) */
typedef struct {
    __typeof__(&subghz_protocol_keeloq_common_decrypt) subghz_protocol_keeloq_common_decrypt;
    __typeof__(&subghz_protocol_keeloq_common_encrypt) subghz_protocol_keeloq_common_encrypt;
    __typeof__(&subghz_protocol_keeloq_common_normal_learning) subghz_protocol_keeloq_common_normal_learning;
} enc_star_line_host_t;
static const void* enc_star_line_host_raw;
#define ENC_STAR_LINE_HOST ((const enc_star_line_host_t*)enc_star_line_host_raw)
#define subghz_protocol_keeloq_common_decrypt (ENC_STAR_LINE_HOST->subghz_protocol_keeloq_common_decrypt)
#define subghz_protocol_keeloq_common_encrypt (ENC_STAR_LINE_HOST->subghz_protocol_keeloq_common_encrypt)
#define subghz_protocol_keeloq_common_normal_learning (ENC_STAR_LINE_HOST->subghz_protocol_keeloq_common_normal_learning)

static uint8_t star_line_btn_to_custom(uint8_t btn);
static uint8_t star_line_custom_to_btn(uint8_t custom, uint8_t original_btn);
static uint8_t star_line_get_btn_code(uint8_t original_btn);
static bool
    subghz_protocol_star_line_gen_data(SubGhzProtocolEncoderStarLine* instance, uint8_t btn);
static bool subghz_protocol_encoder_star_line_get_upload(
    SubGhzProtocolEncoderStarLine* instance,
    uint8_t btn);
static SubGhzProtocolStatus subghz_protocol_encoder_star_line_serialize(
    SubGhzProtocolEncoderStarLine* instance,
    FlipperFormat* flipper_format);
static inline bool subghz_protocol_star_line_check_decrypt(
    SubGhzBlockGeneric* instance,
    uint32_t decrypt,
    uint8_t btn,
    uint32_t end_serial);
static uint8_t subghz_protocol_star_line_check_remote_controller_selector(
    SubGhzBlockGeneric* instance,
    uint32_t fix,
    uint32_t hop,
    SubGhzKeystore* keystore,
    const char** manufacture_name);
static void subghz_protocol_star_line_check_remote_controller(
    SubGhzBlockGeneric* instance,
    SubGhzKeystore* keystore,
    const char** manufacture_name);

static uint8_t star_line_btn_to_custom(uint8_t btn) {
    switch(btn) {
    case 0x01:
    case 0x21: return SUBGHZ_CUSTOM_BTN_UP;
    case 0x02:
    case 0x22: return SUBGHZ_CUSTOM_BTN_DOWN;
    case 0x03:
    case 0x23: return SUBGHZ_CUSTOM_BTN_LEFT;
    case 0x04:
    case 0x24: return SUBGHZ_CUSTOM_BTN_RIGHT;
    default:   return SUBGHZ_CUSTOM_BTN_OK;
    }
}

static uint8_t star_line_custom_to_btn(uint8_t custom, uint8_t original_btn) {
    if(custom == SUBGHZ_CUSTOM_BTN_OK) return original_btn;

    bool is_twage = (original_btn & 0x20) != 0;

    switch(custom) {
    case SUBGHZ_CUSTOM_BTN_UP:    return is_twage ? 0x21 : 0x01;
    case SUBGHZ_CUSTOM_BTN_DOWN:  return is_twage ? 0x22 : 0x02;
    case SUBGHZ_CUSTOM_BTN_LEFT:  return is_twage ? 0x23 : 0x03;
    case SUBGHZ_CUSTOM_BTN_RIGHT: return is_twage ? 0x24 : 0x04;
    default: return original_btn;
    }
}

static uint8_t star_line_get_btn_code(uint8_t original_btn) {
    uint8_t custom = subghz_custom_btn_get();
    bool is_twage  = (original_btn & 0x20) != 0;
    uint8_t page   = subghz_custom_btn_get_page();

    FURI_LOG_I(TAG, "get_btn_code: original=0x%02X custom=%u page=%u is_twage=%d",
        original_btn, custom, page, is_twage);

    if(subghz_custom_btn_get_long()) {
        subghz_custom_btn_set_long(false);
        switch(custom) {
        case SUBGHZ_CUSTOM_BTN_UP:    return is_twage ? 0x25 : 0x01;
        case SUBGHZ_CUSTOM_BTN_DOWN:  return is_twage ? 0x26 : 0x02;
        case SUBGHZ_CUSTOM_BTN_LEFT:  return is_twage ? 0x21 : 0x03;
        case SUBGHZ_CUSTOM_BTN_RIGHT: return is_twage ? 0x22 : 0x04;
        default: return original_btn;
        }
    }

    if(custom == SUBGHZ_CUSTOM_BTN_OK) return original_btn;

    if(!is_twage) {
        return star_line_custom_to_btn(custom, original_btn);
    }

    if(page == 0) {

        switch(custom) {
        case SUBGHZ_CUSTOM_BTN_UP:    return 0x21;
        case SUBGHZ_CUSTOM_BTN_DOWN:  return 0x22;
        case SUBGHZ_CUSTOM_BTN_LEFT:  return 0x23;
        case SUBGHZ_CUSTOM_BTN_RIGHT: return 0x24;
        default: return original_btn;
        }
    } else {

        switch(custom) {
        case SUBGHZ_CUSTOM_BTN_UP:    return 0x25;
        case SUBGHZ_CUSTOM_BTN_DOWN:  return 0x26;
        case SUBGHZ_CUSTOM_BTN_LEFT:  return 0x21;
        case SUBGHZ_CUSTOM_BTN_RIGHT: return 0x22;
        default: return original_btn;
        }
    }
}

static bool
    subghz_protocol_star_line_gen_data(SubGhzProtocolEncoderStarLine* instance, uint8_t btn) {

    uint32_t override_cnt = 0;
    if(subghz_block_generic_global_counter_override_get(&override_cnt)) {
        instance->generic.cnt = override_cnt & 0xFFFF;
    } else {
        uint32_t mult = furi_hal_subghz_get_rolling_counter_mult();
        if((instance->generic.cnt + mult) > 0xFFFF) {
            instance->generic.cnt = 0;
        } else {
            instance->generic.cnt += mult;
        }
    }
    instance->generic.btn = btn;

    uint32_t fix = btn << 24 | instance->generic.serial;
    uint32_t decrypt = btn << 24 | (instance->generic.serial & 0xFF) << 16 | instance->generic.cnt;
    uint32_t hop = 0;
    uint64_t man = 0;
    uint64_t code_found_reverse;
    int res = 0;

    if(instance->manufacture_name == 0x0) {
        instance->manufacture_name = "";
    }

    if(strcmp(instance->manufacture_name, "Unknown") == 0) {
        code_found_reverse = subghz_protocol_blocks_reverse_key(
            instance->generic.data, instance->generic.data_count_bit);
        hop = code_found_reverse & 0x00000000ffffffff;
    } else {
        uint8_t kl_type_en = instance->keystore->kl_type;
        SUBGHZ_KEYSTORE_FOR_EACH(manufacture_code, instance->keystore) {
                res = strcmp(
                    furi_string_get_cstr(manufacture_code->name), instance->manufacture_name);
                if(res == 0) {
                    switch(manufacture_code->type) {
                    case KEELOQ_LEARNING_SIMPLE:
                        hop =
                            subghz_protocol_keeloq_common_encrypt(decrypt, manufacture_code->key);
                        break;
                    case KEELOQ_LEARNING_NORMAL:
                        man = subghz_protocol_keeloq_common_normal_learning(
                            fix, manufacture_code->key);
                        hop = subghz_protocol_keeloq_common_encrypt(decrypt, man);
                        break;
                    case KEELOQ_LEARNING_UNKNOWN:
                        if(kl_type_en == 1) {
                            hop = subghz_protocol_keeloq_common_encrypt(
                                decrypt, manufacture_code->key);
                        }
                        if(kl_type_en == 2) {
                            man = subghz_protocol_keeloq_common_normal_learning(
                                fix, manufacture_code->key);
                            hop = subghz_protocol_keeloq_common_encrypt(decrypt, man);
                        }
                        break;
                    }
                    break;
                }
            }
    }
    if(hop) {
        uint64_t yek = (uint64_t)fix << 32 | hop;
        instance->generic.data =
            subghz_protocol_blocks_reverse_key(yek, instance->generic.data_count_bit);
        return true;
    } else {
        instance->manufacture_name = "Unknown";
        return false;
    }
}

static bool subghz_protocol_encoder_star_line_get_upload(
    SubGhzProtocolEncoderStarLine* instance,
    uint8_t btn) {
    furi_check(instance);

    if(!subghz_protocol_star_line_gen_data(instance, btn)) {
        return false;
    }

    size_t index = 0;
    size_t size_upload = 6 * 2 + (instance->generic.data_count_bit * 2);
    if(size_upload > instance->encoder.size_upload) {
        FURI_LOG_E(TAG, "Size upload exceeds allocated encoder buffer.");
        return false;
    } else {
        instance->encoder.size_upload = size_upload;
    }

    for(uint8_t i = 6; i > 0; i--) {
        instance->encoder.upload[index++] =
            level_duration_make(true, (uint32_t)subghz_protocol_star_line_const.te_long * 2);
        instance->encoder.upload[index++] =
            level_duration_make(false, (uint32_t)subghz_protocol_star_line_const.te_long * 2);
    }

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if(bit_read(instance->generic.data, i - 1)) {
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_star_line_const.te_long);
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_star_line_const.te_long);
        } else {
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_star_line_const.te_short);
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_star_line_const.te_short);
        }
    }

    return true;
}

static SubGhzProtocolStatus subghz_protocol_encoder_star_line_serialize(
    SubGhzProtocolEncoderStarLine* instance,
    FlipperFormat* flipper_format) {
    subghz_protocol_star_line_check_remote_controller(
        &instance->generic, instance->keystore, &instance->manufacture_name);

    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    do {
        if(!flipper_format_insert_or_update_string_cstr(
               flipper_format, "Protocol", instance->generic.protocol_name)) {
            break;
        }

        uint32_t bits = instance->generic.data_count_bit;
        if(!flipper_format_insert_or_update_uint32(flipper_format, "Bit", &bits, 1)) {
            break;
        }

        char key_str[20];
        snprintf(key_str, sizeof(key_str), "%016llX", instance->generic.data);
        if(!flipper_format_insert_or_update_string_cstr(flipper_format, "Key", key_str)) {
            break;
        }

        if(!flipper_format_insert_or_update_uint32(
               flipper_format, "Serial", &instance->generic.serial, 1)) {
            break;
        }

        uint32_t temp = instance->generic.btn;
        if(!flipper_format_insert_or_update_uint32(flipper_format, "Btn", &temp, 1)) {
            break;
        }

        if(!flipper_format_insert_or_update_uint32(
               flipper_format, "Cnt", &instance->generic.cnt, 1)) {
            break;
        }

        if(!flipper_format_insert_or_update_string_cstr(
               flipper_format, "Manufacture", instance->manufacture_name)) {
            break;
        }

        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

static inline bool subghz_protocol_star_line_check_decrypt(
    SubGhzBlockGeneric* instance,
    uint32_t decrypt,
    uint8_t btn,
    uint32_t end_serial) {
    furi_check(instance);
    if((decrypt >> 24 == btn) && ((((uint16_t)(decrypt >> 16)) & 0x00FF) == end_serial)) {
        instance->cnt = decrypt & 0x0000FFFF;
        return true;
    }
    return false;
}

static uint8_t subghz_protocol_star_line_check_remote_controller_selector(
    SubGhzBlockGeneric* instance,
    uint32_t fix,
    uint32_t hop,
    SubGhzKeystore* keystore,
    const char** manufacture_name) {
    uint16_t end_serial = (uint16_t)(fix & 0xFF);
    uint8_t btn = (uint8_t)(fix >> 24);
    uint32_t decrypt = 0;
    uint64_t man_normal_learning;
    bool mf_not_set = false;
    if(keystore->mfname == NULL) {
        subghz_keystore_reset_kl(keystore);
    }

    const char* mfname = keystore->mfname;

    if(strcmp(mfname, "Unknown") == 0) {
        return 1;
    } else if(strcmp(mfname, "") == 0) {
        mf_not_set = true;
    }
    SUBGHZ_KEYSTORE_FOR_EACH(manufacture_code, keystore) {
            if(mf_not_set || (strcmp(furi_string_get_cstr(manufacture_code->name), mfname) == 0)) {
                switch(manufacture_code->type) {
                case KEELOQ_LEARNING_SIMPLE:
                    decrypt = subghz_protocol_keeloq_common_decrypt(hop, manufacture_code->key);
                    if(subghz_protocol_star_line_check_decrypt(
                           instance, decrypt, btn, end_serial)) {
                        *manufacture_name =
                            subghz_keystore_keep_name(keystore, furi_string_get_cstr(manufacture_code->name));
                        keystore->mfname = *manufacture_name;
                        return 1;
                    }
                    break;
                case KEELOQ_LEARNING_NORMAL:
                    man_normal_learning =
                        subghz_protocol_keeloq_common_normal_learning(fix, manufacture_code->key);
                    decrypt = subghz_protocol_keeloq_common_decrypt(hop, man_normal_learning);
                    if(subghz_protocol_star_line_check_decrypt(
                           instance, decrypt, btn, end_serial)) {
                        *manufacture_name =
                            subghz_keystore_keep_name(keystore, furi_string_get_cstr(manufacture_code->name));
                        keystore->mfname = *manufacture_name;
                        return 1;
                    }
                    break;
                case KEELOQ_LEARNING_UNKNOWN:
                    decrypt = subghz_protocol_keeloq_common_decrypt(hop, manufacture_code->key);
                    if(subghz_protocol_star_line_check_decrypt(
                           instance, decrypt, btn, end_serial)) {
                        *manufacture_name =
                            subghz_keystore_keep_name(keystore, furi_string_get_cstr(manufacture_code->name));
                        keystore->mfname = *manufacture_name;
                        keystore->kl_type = 1;
                        return 1;
                    }
                    uint64_t man_rev = 0;
                    uint64_t man_rev_byte = 0;
                    for(uint8_t i = 0; i < 64; i += 8) {
                        man_rev_byte = (uint8_t)(manufacture_code->key >> i);
                        man_rev = man_rev | man_rev_byte << (56 - i);
                    }
                    decrypt = subghz_protocol_keeloq_common_decrypt(hop, man_rev);
                    if(subghz_protocol_star_line_check_decrypt(
                           instance, decrypt, btn, end_serial)) {
                        *manufacture_name =
                            subghz_keystore_keep_name(keystore, furi_string_get_cstr(manufacture_code->name));
                        keystore->mfname = *manufacture_name;
                        keystore->kl_type = 1;
                        return 1;
                    }
                    man_normal_learning =
                        subghz_protocol_keeloq_common_normal_learning(fix, manufacture_code->key);
                    decrypt = subghz_protocol_keeloq_common_decrypt(hop, man_normal_learning);
                    if(subghz_protocol_star_line_check_decrypt(
                           instance, decrypt, btn, end_serial)) {
                        *manufacture_name =
                            subghz_keystore_keep_name(keystore, furi_string_get_cstr(manufacture_code->name));
                        keystore->mfname = *manufacture_name;
                        keystore->kl_type = 2;
                        return 1;
                    }
                    man_normal_learning =
                        subghz_protocol_keeloq_common_normal_learning(fix, man_rev);
                    decrypt = subghz_protocol_keeloq_common_decrypt(hop, man_normal_learning);
                    if(subghz_protocol_star_line_check_decrypt(
                           instance, decrypt, btn, end_serial)) {
                        *manufacture_name =
                            subghz_keystore_keep_name(keystore, furi_string_get_cstr(manufacture_code->name));
                        keystore->mfname = *manufacture_name;
                        keystore->kl_type = 2;
                        return 1;
                    }
                    break;
                }
            }
        }

    *manufacture_name = "Unknown";
    keystore->mfname = "Unknown";
    instance->cnt = 0;

    return 0;
}

static void subghz_protocol_star_line_check_remote_controller(
    SubGhzBlockGeneric* instance,
    SubGhzKeystore* keystore,
    const char** manufacture_name) {
    uint64_t key = subghz_protocol_blocks_reverse_key(instance->data, instance->data_count_bit);
    uint32_t key_fix = key >> 32;
    uint32_t key_hop = key & 0x00000000ffffffff;

    subghz_protocol_star_line_check_remote_controller_selector(
        instance, key_fix, key_hop, keystore, manufacture_name);

    instance->serial = key_fix & 0x00FFFFFF;
    instance->btn = key_fix >> 24;
}

void* subghz_protocol_encoder_star_line_alloc(SubGhzEnvironment* environment) {
    SubGhzProtocolEncoderStarLine* instance = malloc(sizeof(SubGhzProtocolEncoderStarLine));

    instance->base.protocol = &subghz_protocol_star_line;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->keystore = subghz_environment_get_keystore(environment);

    instance->manufacture_from_file = furi_string_alloc();

    instance->encoder.repeat = 40;
    instance->encoder.size_upload = 256;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;

    return instance;
}

void subghz_protocol_encoder_star_line_free(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderStarLine* instance = context;
    furi_string_free(instance->manufacture_from_file);
    free(instance->encoder.upload);
    free(instance);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_star_line_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderStarLine* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    flipper_format_rewind(flipper_format);

    do {
        FuriString* temp_str = furi_string_alloc();
        if(!flipper_format_read_string(flipper_format, "Protocol", temp_str)) {
            FURI_LOG_E(TAG, "Missing Protocol");
            furi_string_free(temp_str);
            break;
        }

        if(!furi_string_equal(temp_str, instance->base.protocol->name)) {
            FURI_LOG_E(
                TAG,
                "Wrong protocol %s != %s",
                furi_string_get_cstr(temp_str),
                instance->base.protocol->name);
            furi_string_free(temp_str);
            break;
        }
        furi_string_free(temp_str);

        uint32_t bit_count_temp;
        if(!flipper_format_read_uint32(flipper_format, "Bit", &bit_count_temp, 1)) {
            FURI_LOG_E(TAG, "Missing Bit");
            break;
        }

        instance->generic.data_count_bit = subghz_protocol_star_line_const.min_count_bit_for_found;

        temp_str = furi_string_alloc();
        if(!flipper_format_read_string(flipper_format, "Key", temp_str)) {
            FURI_LOG_E(TAG, "Missing Key");
            furi_string_free(temp_str);
            break;
        }

        const char* key_str = furi_string_get_cstr(temp_str);
        uint64_t key = 0;
        size_t str_len = strlen(key_str);
        size_t hex_pos = 0;
        for(size_t i = 0; i < str_len && hex_pos < 16; i++) {
            char c = key_str[i];
            if(c == ' ') continue;

            uint8_t nibble;
            if(c >= '0' && c <= '9') {
                nibble = c - '0';
            } else if(c >= 'A' && c <= 'F') {
                nibble = c - 'A' + 10;
            } else if(c >= 'a' && c <= 'f') {
                nibble = c - 'a' + 10;
            } else {
                FURI_LOG_E(TAG, "Invalid hex character: %c", c);
                furi_string_free(temp_str);
                break;
            }

            key = (key << 4) | nibble;
            hex_pos++;
        }

        furi_string_free(temp_str);

        if(hex_pos != 16) {
            FURI_LOG_E(TAG, "Invalid key length: %zu nibbles (expected 16)", hex_pos);
            break;
        }

        instance->generic.data = key;
        FURI_LOG_I(TAG, "Parsed key: 0x%016llX", instance->generic.data);

        if(instance->generic.data == 0) {
            FURI_LOG_E(TAG, "Key is zero after parsing!");
            break;
        }

        if(!flipper_format_read_uint32(flipper_format, "Serial", &instance->generic.serial, 1)) {
            instance->generic.serial = instance->generic.data >> 24;
            FURI_LOG_I(TAG, "Extracted serial: 0x%08lX", instance->generic.serial);
        } else {
            FURI_LOG_I(TAG, "Read serial: 0x%08lX", instance->generic.serial);
        }

        uint32_t btn_temp;
        if(flipper_format_read_uint32(flipper_format, "Btn", &btn_temp, 1)) {
            instance->generic.btn = (uint8_t)btn_temp;
            FURI_LOG_I(TAG, "Read button: 0x%02X", instance->generic.btn);
        } else {
            instance->generic.btn = (instance->generic.data >> 16) & 0xFF;
            FURI_LOG_I(TAG, "Extracted button: 0x%02X", instance->generic.btn);
        }

        subghz_custom_btn_set_original(star_line_btn_to_custom(instance->generic.btn));
        subghz_custom_btn_set_max(4);
        subghz_custom_btn_set_pages(true);
        subghz_custom_btn_set_max_pages(2);

        uint32_t cnt_temp;
        if(flipper_format_read_uint32(flipper_format, "Cnt", &cnt_temp, 1)) {
            instance->generic.cnt = (uint16_t)cnt_temp;
            FURI_LOG_I(TAG, "Read counter: 0x%03lX", (unsigned long)instance->generic.cnt);
        }

        if(!flipper_format_read_uint32(
               flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1)) {
            instance->encoder.repeat = 40;
            FURI_LOG_D(
                TAG, "Repeat not found in file, using default 40 for continuous transmission");
        }
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }
        if(flipper_format_read_string(
               flipper_format, "Manufacture", instance->manufacture_from_file)) {
            instance->manufacture_name = furi_string_get_cstr(instance->manufacture_from_file);
            instance->keystore->mfname = instance->manufacture_name;
        } else {
            FURI_LOG_D(TAG, "ENCODER: Missing Manufacture");
        }

        subghz_protocol_star_line_check_remote_controller(
            &instance->generic, instance->keystore, &instance->manufacture_name);

        uint8_t selected_btn = star_line_get_btn_code(instance->generic.btn);
        subghz_block_generic_global_button_override_get(&selected_btn);
        subghz_protocol_encoder_star_line_get_upload(instance, selected_btn);

        subghz_protocol_encoder_star_line_serialize(instance, flipper_format);

        instance->encoder.is_running = true;

        FURI_LOG_I(
            TAG,
            "Encoder deserialized: repeat=%u, size_upload=%zu, is_running=%d, front=%zu",
            instance->encoder.repeat,
            instance->encoder.size_upload,
            instance->encoder.is_running,
            instance->encoder.front);

        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

void subghz_protocol_encoder_star_line_stop(void* context) {
    SubGhzProtocolEncoderStarLine* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_star_line_yield(void* context) {
    SubGhzProtocolEncoderStarLine* instance = context;

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

static const SubGhzProtocolEncoder enc_star_line_vt = {
    .alloc = subghz_protocol_encoder_star_line_alloc,
    .free = subghz_protocol_encoder_star_line_free,
    .deserialize = subghz_protocol_encoder_star_line_deserialize,
    .stop = subghz_protocol_encoder_star_line_stop,
    .yield = subghz_protocol_encoder_star_line_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_star_line = {
    .name = SUBGHZ_PROTOCOL_STAR_LINE_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_star_line_vt,
};

static const SubGhzEncodePluginEntry enc_star_line_entry = {.name = SUBGHZ_PROTOCOL_STAR_LINE_NAME, .encoder = &enc_star_line_vt, .host_slot = &enc_star_line_host_raw};
static const FlipperAppPluginDescriptor enc_star_line_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_star_line_entry};
const FlipperAppPluginDescriptor* enc_star_line_ep(void) { return &enc_star_line_descriptor; }
