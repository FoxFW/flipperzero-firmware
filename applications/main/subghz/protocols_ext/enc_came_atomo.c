/* enc_came_atomo.fal : SD encoder for came_atomo (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define atomo_decrypt enc_came_atomo__atomo_decrypt
#define atomo_encrypt enc_came_atomo__atomo_encrypt
#define subghz_protocol_came_atomo enc_came_atomo__subghz_protocol_came_atomo
#define subghz_protocol_encoder_came_atomo_alloc enc_came_atomo__subghz_protocol_encoder_came_atomo_alloc
#define subghz_protocol_encoder_came_atomo_deserialize enc_came_atomo__subghz_protocol_encoder_came_atomo_deserialize
#define subghz_protocol_encoder_common_alloc enc_came_atomo__subghz_protocol_encoder_common_alloc
#define subghz_protocol_encoder_common_free enc_came_atomo__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_came_atomo__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_came_atomo__subghz_protocol_encoder_common_yield
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/came_atomo.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocoCameAtomo"

struct SubGhzProtocolEncoderCameAtomo {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_came_atomo_const = {
    .te_short = 600,
    .te_long = 1200,
    .te_delta = 250,
    .min_count_bit_for_found = 62,
};

/* host bridge: live firmware functions/state this encoder needs (set by the loader) */
extern bool bypass__bridge_ty;
extern uint8_t came_atomo_counter_mode__bridge_ty;
extern bool came_atomo_raw_replay__bridge_ty;
typedef struct {
    void* bypass;
    void* came_atomo_counter_mode;
    void* came_atomo_raw_replay;
} enc_came_atomo_host_t;
static const void* enc_came_atomo_host_raw;
#define ENC_CAME_ATOMO_HOST ((const enc_came_atomo_host_t*)enc_came_atomo_host_raw)
#define bypass (*(__typeof__(bypass__bridge_ty)*)(ENC_CAME_ATOMO_HOST->bypass))
#define came_atomo_counter_mode (*(__typeof__(came_atomo_counter_mode__bridge_ty)*)(ENC_CAME_ATOMO_HOST->came_atomo_counter_mode))
#define came_atomo_raw_replay (*(__typeof__(came_atomo_raw_replay__bridge_ty)*)(ENC_CAME_ATOMO_HOST->came_atomo_raw_replay))

static LevelDuration
    subghz_protocol_encoder_came_atomo_add_duration_to_upload(ManchesterEncoderResult result);
static void subghz_protocol_encoder_came_atomo_get_upload(
    SubGhzProtocolEncoderCameAtomo* instance,
    uint8_t btn);
static void subghz_protocol_came_atomo_remote_controller(SubGhzBlockGeneric* instance);
static uint8_t subghz_protocol_came_atomo_get_btn_code(void);

static LevelDuration
    subghz_protocol_encoder_came_atomo_add_duration_to_upload(ManchesterEncoderResult result) {
    LevelDuration data = {.duration = 0, .level = 0};
    switch(result) {
    case ManchesterEncoderResultShortLow:
        data.duration = subghz_protocol_came_atomo_const.te_short;
        data.level = false;
        break;
    case ManchesterEncoderResultLongLow:
        data.duration = subghz_protocol_came_atomo_const.te_long;
        data.level = false;
        break;
    case ManchesterEncoderResultLongHigh:
        data.duration = subghz_protocol_came_atomo_const.te_long;
        data.level = true;
        break;
    case ManchesterEncoderResultShortHigh:
        data.duration = subghz_protocol_came_atomo_const.te_short;
        data.level = true;
        break;

    default:
        FURI_LOG_E(TAG, "SubGhz: ManchesterEncoderResult is incorrect.");
        break;
    }
    return level_duration_make(data.level, data.duration);
}

static void subghz_protocol_encoder_came_atomo_get_upload(
    SubGhzProtocolEncoderCameAtomo* instance,
    uint8_t btn) {
    furi_assert(instance);
    size_t index = 0;

    ManchesterEncoderState enc_state;
    manchester_encoder_reset(&enc_state);
    ManchesterEncoderResult result;

    uint8_t pack[8] = {};

    if(subghz_block_generic_global.cnt_need_override ||
       subghz_block_generic_global.btn_need_override)
        bypass = true;

    if(came_atomo_counter_mode == 0 || bypass) {

        if(came_atomo_raw_replay) {
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
    } else if(came_atomo_counter_mode == 1) {

        if((instance->generic.cnt + 0x1) > 0xFFFF) {
            instance->generic.cnt = 0;
        } else if(instance->generic.cnt >= 0x1 && instance->generic.cnt != 0xFFFE) {
            instance->generic.cnt = 0xFFFE;
        } else {
            instance->generic.cnt++;
        }
    } else if(came_atomo_counter_mode == 2) {

        if(instance->generic.cnt != 0x807B && instance->generic.cnt != 0x807C &&
           instance->generic.cnt != 0x007B) {
            instance->generic.cnt = 0x807B;
        } else if(instance->generic.cnt == 0x807C) {
            instance->generic.cnt = 0x007B;
        } else {
            instance->generic.cnt++;
        }
    } else {

    }

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(btn);
    }

    btn = subghz_protocol_came_atomo_get_btn_code();

    if(btn == 0x1) {
        btn = 0x0;
    } else if(btn == 0x2) {
        btn = 0x2;
    } else if(btn == 0x3) {
        btn = 0x4;
    } else if(btn == 0x4) {
        btn = 0x6;
    }

    if(subghz_block_generic_global_button_override_get(&btn))
        FURI_LOG_D(TAG, "Button sucessfully changed to 0x%X", btn);

    instance->encoder.upload[index++] =
        level_duration_make(true, (uint32_t)subghz_protocol_came_atomo_const.te_long * 15);
    instance->encoder.upload[index++] =
        level_duration_make(false, (uint32_t)subghz_protocol_came_atomo_const.te_long * 60);

    pack[0] = 0;
    for(uint8_t i = 0; i < 8; i++) {

        pack[1] = (instance->generic.cnt >> 8);
        pack[2] = (instance->generic.cnt & 0xFF);
        pack[3] = ((instance->generic.data_2 >> 32) & 0xFF);
        pack[4] = ((instance->generic.data_2 >> 24) & 0xFF);
        pack[5] = ((instance->generic.data_2 >> 16) & 0xFF);
        pack[6] = ((instance->generic.data_2 >> 8) & 0xFF);
        pack[7] = (btn << 4);

        switch(i) {
        case 0:
            pack[0] = 10;
            break;
        case 1:
            pack[0] = 30;
            break;
        case 2:
            pack[0] = 125;
            break;
        case 3:
            pack[0] = 126;
            break;
        case 4:
            pack[0] = 127;
            break;
        case 5:
            pack[0] = 0;
            break;
        case 6:
            pack[0] = 1;
            break;
        case 7:
            pack[0] = 3;
            break;

        default:
            break;
        }

        atomo_encrypt(pack);
        uint32_t hi = pack[0] << 24 | pack[1] << 16 | pack[2] << 8 | pack[3];
        uint32_t lo = pack[4] << 24 | pack[5] << 16 | pack[6] << 8 | pack[7];
        instance->generic.data = (uint64_t)hi << 32 | lo;

        instance->generic.data ^= 0xFFFFFFFFFFFFFFFF;
        instance->generic.data >>= 4;
        instance->generic.data &= 0xFFFFFFFFFFFFFFF;

        instance->encoder.upload[index++] =
            level_duration_make(true, (uint32_t)subghz_protocol_came_atomo_const.te_long);
        instance->encoder.upload[index++] =
            level_duration_make(false, (uint32_t)subghz_protocol_came_atomo_const.te_short);

        for(uint8_t i = (instance->generic.data_count_bit - 2); i > 0; i--) {
            if(!manchester_encoder_advance(
                   &enc_state, !bit_read(instance->generic.data, i - 1), &result)) {
                instance->encoder.upload[index++] =
                    subghz_protocol_encoder_came_atomo_add_duration_to_upload(result);
                manchester_encoder_advance(
                    &enc_state, !bit_read(instance->generic.data, i - 1), &result);
            }
            instance->encoder.upload[index++] =
                subghz_protocol_encoder_came_atomo_add_duration_to_upload(result);
        }
        instance->encoder.upload[index] =
            subghz_protocol_encoder_came_atomo_add_duration_to_upload(
                manchester_encoder_finish(&enc_state));
        if(level_duration_get_level(instance->encoder.upload[index])) {
            index++;
        }

        instance->encoder.upload[index++] =
            level_duration_make(false, (uint32_t)subghz_protocol_came_atomo_const.te_delta * 272);
    }
    instance->encoder.size_upload = index;
    instance->generic.cnt_2++;
    pack[0] = (instance->generic.cnt_2);
    pack[1] = (instance->generic.cnt >> 8);
    pack[2] = (instance->generic.cnt & 0xFF);
    pack[3] = ((instance->generic.data_2 >> 32) & 0xFF);
    pack[4] = ((instance->generic.data_2 >> 24) & 0xFF);
    pack[5] = ((instance->generic.data_2 >> 16) & 0xFF);
    pack[6] = ((instance->generic.data_2 >> 8) & 0xFF);
    pack[7] = (btn << 4);

    atomo_encrypt(pack);
    uint32_t hi = pack[0] << 24 | pack[1] << 16 | pack[2] << 8 | pack[3];
    uint32_t lo = pack[4] << 24 | pack[5] << 16 | pack[6] << 8 | pack[7];
    instance->generic.data = (uint64_t)hi << 32 | lo;

    instance->generic.data ^= 0xFFFFFFFFFFFFFFFF;
    instance->generic.data >>= 4;
    instance->generic.data &= 0xFFFFFFFFFFFFFFF;
}

static void subghz_protocol_came_atomo_remote_controller(SubGhzBlockGeneric* instance) {

    instance->data ^= 0xFFFFFFFFFFFFFFFF;
    instance->data <<= 4;

    uint8_t pack[8] = {};
    pack[0] = (instance->data >> 56);
    pack[1] = ((instance->data >> 48) & 0xFF);
    pack[2] = ((instance->data >> 40) & 0xFF);
    pack[3] = ((instance->data >> 32) & 0xFF);
    pack[4] = ((instance->data >> 24) & 0xFF);
    pack[5] = ((instance->data >> 16) & 0xFF);
    pack[6] = ((instance->data >> 8) & 0xFF);
    pack[7] = (instance->data & 0xFF);

    atomo_decrypt(pack);

    instance->cnt_2 = pack[0];
    instance->cnt = (uint16_t)pack[1] << 8 | pack[2];
    instance->serial = (uint32_t)(pack[3]) << 24 | pack[4] << 16 | pack[5] << 8 | pack[6];

    uint8_t btn_decode = (pack[7] >> 4);
    if(btn_decode == 0x0) {
        instance->btn = 0x1;
    } else if(btn_decode == 0x2) {
        instance->btn = 0x2;
    } else if(btn_decode == 0x4) {
        instance->btn = 0x3;
    } else if(btn_decode == 0x6) {
        instance->btn = 0x4;
    }

    uint32_t hi = pack[0] << 24 | pack[1] << 16 | pack[2] << 8 | pack[3];
    uint32_t lo = pack[4] << 24 | pack[5] << 16 | pack[6] << 8 | pack[7];
    instance->data_2 = (uint64_t)hi << 32 | lo;

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->btn);
    }
    subghz_custom_btn_set_max(3);
}

static uint8_t subghz_protocol_came_atomo_get_btn_code(void) {
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
        case 0x3:
            btn = 0x1;
            break;
        case 0x4:
            btn = 0x1;
            break;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_DOWN) {
        switch(original_btn_code) {
        case 0x1:
            btn = 0x3;
            break;
        case 0x2:
            btn = 0x3;
            break;
        case 0x3:
            btn = 0x2;
            break;
        case 0x4:
            btn = 0x2;
            break;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_LEFT) {
        switch(original_btn_code) {
        case 0x1:
            btn = 0x4;
            break;
        case 0x2:
            btn = 0x4;
            break;
        case 0x3:
            btn = 0x4;
            break;
        case 0x4:
            btn = 0x3;
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

void* subghz_protocol_encoder_came_atomo_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderCameAtomo), &subghz_protocol_came_atomo, 1, 900);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_came_atomo_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderCameAtomo* instance = context;
    SubGhzProtocolStatus res = SubGhzProtocolStatusError;
    do {
        /* #16: TxRaw verbatim replay — freeze the rolling counter. */
        came_atomo_raw_replay = false;
        {
            uint32_t came_atomo_txraw = 0;
            flipper_format_rewind(flipper_format);
            if(flipper_format_read_uint32(flipper_format, "TxRaw", &came_atomo_txraw, 1) &&
               came_atomo_txraw != 0) {
                came_atomo_raw_replay = true;
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
            came_atomo_counter_mode = (uint8_t)tmp_counter_mode;
        } else {
            came_atomo_counter_mode = 0;
        }

        subghz_protocol_came_atomo_remote_controller(&instance->generic);
        subghz_protocol_encoder_came_atomo_get_upload(instance, instance->generic.btn);

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

void atomo_encrypt(uint8_t* buff) {
    uint8_t tmpB = (~buff[0] + 1) & 0x7F;

    uint8_t bitCnt = 8;
    while(bitCnt < 59) {
        if((tmpB & 0x18) && (((tmpB / 8) & 3) != 3)) {
            tmpB = ((tmpB << 1) & 0xFF) | 1;
        } else {
            tmpB = (tmpB << 1) & 0xFF;
        }

        if(tmpB & 0x80) {
            buff[bitCnt / 8] ^= (0x80 >> (bitCnt & 7));
        }

        bitCnt++;
    }

    buff[0] = (buff[0] ^ 5) & 0x7F;
}

void atomo_decrypt(uint8_t* buff) {
    buff[0] = (buff[0] ^ 5) & 0x7F;
    uint8_t tmpB = (-buff[0]) & 0x7F;

    uint8_t bitCnt = 8;
    while(bitCnt < 59) {
        if((tmpB & 0x18) && (((tmpB / 8) & 3) != 3)) {
            tmpB = ((tmpB << 1) & 0xFF) | 1;
        } else {
            tmpB = (tmpB << 1) & 0xFF;
        }

        if(tmpB & 0x80) {
            buff[bitCnt / 8] ^= (0x80 >> (bitCnt & 7));
        }

        bitCnt++;
    }
}

static const SubGhzProtocolEncoder enc_came_atomo_vt = {
    .alloc = subghz_protocol_encoder_came_atomo_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_came_atomo_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_came_atomo = {
    .name = SUBGHZ_PROTOCOL_CAME_ATOMO_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = NULL,
    .encoder = &enc_came_atomo_vt,
};

static const SubGhzEncodePluginEntry enc_came_atomo_entry = {.name = SUBGHZ_PROTOCOL_CAME_ATOMO_NAME, .encoder = &enc_came_atomo_vt, .host_slot = &enc_came_atomo_host_raw};
static const FlipperAppPluginDescriptor enc_came_atomo_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_came_atomo_entry};
const FlipperAppPluginDescriptor* enc_came_atomo_ep(void) { return &enc_came_atomo_descriptor; }
