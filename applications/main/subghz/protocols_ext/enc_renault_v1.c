

#define renault_v1_iv_to_u32 enc_renault_v1__renault_v1_iv_to_u32
#define subghz_protocol_encoder_renault_v1_alloc enc_renault_v1__subghz_protocol_encoder_renault_v1_alloc
#define subghz_protocol_encoder_renault_v1_deserialize enc_renault_v1__subghz_protocol_encoder_renault_v1_deserialize
#define subghz_protocol_renault_v1 enc_renault_v1__subghz_protocol_renault_v1
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/renault_v1.h>
#include <lib/subghz/protocols/renault_v1_crypto.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/common.h>

#define RENAULT_V1_LO_GAP 1500
#define RENAULT_V1_HI_GAP 1000
#define RENAULT_V1_PRE_COUNT       250
#define RENAULT_V1_SYNC_BIT_COUNT 16
#define RENAULT_V1_CHECK_BIT_COUNT 10
#define RENAULT_V1_SYNC_WORD       0x0001
#define RENAULT_V1_DATA_1_SIZE     64
#define RENAULT_V1_DATA_2_SIZE     24
#define RENAULT_V1_DATA_REPEAT     3
#define RENAULT_V1_TAIL_REPEAT_GAP 21500
#define RENAULT_V1_CUSTOM_BTN_MAX  2
#define RENAULT_V1_COUNTER_BITS    16
#define RENAULT_V1_COUNTER_MAX     ((1u << RENAULT_V1_COUNTER_BITS) - 1)

enum {
    RENAULT_V1_DATA_FRAME_ENTRIES =
        2 + (RENAULT_V1_SYNC_BIT_COUNT + RENAULT_V1_DATA_1_SIZE + RENAULT_V1_DATA_2_SIZE) * 2,
    RENAULT_V1_TAIL_FRAME_ENTRIES =
        2 + (RENAULT_V1_SYNC_BIT_COUNT + RENAULT_V1_CHECK_BIT_COUNT) * 2 + 1,
    RENAULT_V1_UPLOAD_SIZE = RENAULT_V1_PRE_COUNT * 2 +
                             RENAULT_V1_DATA_REPEAT * RENAULT_V1_DATA_FRAME_ENTRIES +
                             RENAULT_V1_DATA_REPEAT * RENAULT_V1_TAIL_FRAME_ENTRIES,
};

struct SubGhzProtocolEncoderRenaultV1 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_renault_v1_const = {
    .te_short = 125,
    .te_long = 250,
    .te_delta = 100,
    .min_count_bit_for_found = 88,
};

static const uint8_t renault_v1_btns[] = {0x0, 0x1, 0x2, 0x4, 0x8};

static const uint8_t renault_v1_btn_map[RENAULT_V1_CUSTOM_BTN_MAX][5] = {
    {0x1, 0x1, 0x1, 0x1, 0x1},
    {0x2, 0x2, 0x2, 0x2, 0x2},
};

typedef struct {
    __typeof__(&subghz_protocol_renault_v1_encrypt) subghz_protocol_renault_v1_encrypt;
} enc_renault_v1_host_t;
static const void* enc_renault_v1_host_raw;
#define ENC_RENAULT_V1_HOST ((const enc_renault_v1_host_t*)enc_renault_v1_host_raw)
#define subghz_protocol_renault_v1_encrypt (ENC_RENAULT_V1_HOST->subghz_protocol_renault_v1_encrypt)

static void subghz_protocol_renault_v1_check_remote_controller(SubGhzBlockGeneric* instance);

static size_t renault_v1_add_bit(LevelDuration* upload, size_t index, bool bit);

static size_t
    renault_v1_add_bits(LevelDuration* upload, size_t index, uint64_t value, uint8_t count);
static bool subghz_protocol_encoder_renault_v1_get_upload(SubGhzProtocolEncoderRenaultV1* instance);

static void subghz_protocol_renault_v1_check_remote_controller(SubGhzBlockGeneric* instance) {
    instance->serial = (uint32_t)(instance->data >> 32);
    instance->btn = (uint8_t)((instance->data >> 28) & 0x0F);

    subghz_custom_btn_set_max((instance->status == 1) ? RENAULT_V1_CUSTOM_BTN_MAX : 0);

    if(instance->status == 1) {
        const uint32_t iv = instance->seed;
        instance->cnt =
            ((iv >> 28) & 0x0F) | (((iv >> 16) & 0xFF) << 4) | (((iv >> 8) & 0x0F) << 12);
    } else {
        instance->cnt = 0;
    }
}

static size_t renault_v1_add_bit(LevelDuration* upload, size_t index, bool bit) {
    const uint32_t te = subghz_protocol_renault_v1_const.te_short;
    upload[index++] = level_duration_make(bit, te);
    upload[index++] = level_duration_make(!bit, te);
    return index;
}

static size_t
    renault_v1_add_bits(LevelDuration* upload, size_t index, uint64_t value, uint8_t count) {
    while(count) {
        count--;
        index = renault_v1_add_bit(upload, index, (value >> count) & 1);
    }
    return index;
}

static bool subghz_protocol_encoder_renault_v1_get_upload(SubGhzProtocolEncoderRenaultV1* instance) {
    furi_assert(instance);
    size_t index = 0;

    subghz_custom_btn_set_original_if_unset(instance->generic.btn);

    subghz_custom_btn_apply_map(
        &instance->generic.btn, subghz_custom_btn_get_original(), subghz_custom_btn_get(),
        renault_v1_btns, (const uint8_t*)renault_v1_btn_map, COUNT_OF(renault_v1_btns),
        RENAULT_V1_CUSTOM_BTN_MAX);

    subghz_block_generic_global_button_override_get(&instance->generic.btn);

    if(instance->generic.status == 1) {
        subghz_block_generic_advance_counter(&instance->generic.cnt, RENAULT_V1_COUNTER_MAX);
        const uint16_t cnt = (uint16_t)(instance->generic.cnt & RENAULT_V1_COUNTER_MAX);
        const uint8_t button = (uint8_t)(instance->generic.btn & 0x0F);

        const uint8_t iv[4] = {
            (uint8_t)(((cnt & 0x0F) << 4) | button),
            (uint8_t)((cnt >> 4) & 0xFF),
            (uint8_t)(((cnt >> 12) & 0x0F) | ((uint8_t)(instance->generic.seed >> 8) & 0xF0)),
            (uint8_t)(instance->generic.seed)};
        const uint8_t serial[4] = {
            (uint8_t)(instance->generic.data >> 56),
            (uint8_t)(instance->generic.data >> 48),
            (uint8_t)(instance->generic.data >> 40),
            (uint8_t)(instance->generic.data >> 32)};
        uint8_t pay[11];
        subghz_protocol_renault_v1_encrypt(serial, iv, pay);
        uint64_t d = 0, d2 = 0;
        for(int i = 0; i < 8; i++) d = (d << 8) | pay[i];
        for(int i = 8; i < 11; i++) d2 = (d2 << 8) | pay[i];
        instance->generic.data = d;
        instance->generic.data_2 = d2;
        instance->generic.seed = renault_v1_iv_to_u32(iv);
    }

    for(int p = 0; p < RENAULT_V1_PRE_COUNT; p++) {
        instance->encoder.upload[index++] =
            level_duration_make(true, subghz_protocol_renault_v1_const.te_short);
        instance->encoder.upload[index++] =
            level_duration_make(false, subghz_protocol_renault_v1_const.te_short);
    }

    for(int r = 0; r < RENAULT_V1_DATA_REPEAT; r++) {
        instance->encoder.upload[index++] = level_duration_make(false, RENAULT_V1_LO_GAP);
        instance->encoder.upload[index++] = level_duration_make(true, RENAULT_V1_HI_GAP);
        index = renault_v1_add_bits(
            instance->encoder.upload, index, RENAULT_V1_SYNC_WORD, RENAULT_V1_SYNC_BIT_COUNT);
        index = renault_v1_add_bits(
            instance->encoder.upload, index, instance->generic.data, RENAULT_V1_DATA_1_SIZE);
        index = renault_v1_add_bits(
            instance->encoder.upload, index, instance->generic.data_2, RENAULT_V1_DATA_2_SIZE);
    }

    const uint8_t payload_b4 = (uint8_t)(instance->generic.data >> 24);
    const uint8_t payload_b5 = (uint8_t)(instance->generic.data >> 16);
    const uint8_t tail_hi = (uint8_t)(((payload_b4 << 4) & 0xF0) | (payload_b5 >> 4));
    const uint8_t tail_lo = (uint8_t)((payload_b5 << 4) & 0xF0);
    const uint16_t tail = (uint16_t)(((uint16_t)tail_hi << 2) | (tail_lo >> 6));

    for(int t = 0; t < RENAULT_V1_DATA_REPEAT; t++) {
        instance->encoder.upload[index++] = level_duration_make(false, RENAULT_V1_LO_GAP);
        instance->encoder.upload[index++] = level_duration_make(true, RENAULT_V1_HI_GAP);
        index = renault_v1_add_bits(
            instance->encoder.upload, index, RENAULT_V1_SYNC_WORD, RENAULT_V1_SYNC_BIT_COUNT);
        index = renault_v1_add_bits(
            instance->encoder.upload, index, tail, RENAULT_V1_CHECK_BIT_COUNT);
        instance->encoder.upload[index++] = level_duration_make(false, RENAULT_V1_TAIL_REPEAT_GAP);
    }

    furi_check(index <= RENAULT_V1_UPLOAD_SIZE);
    instance->encoder.size_upload = index;
    return true;
}

uint32_t renault_v1_iv_to_u32(const uint8_t iv[4]) {
    return ((uint32_t)iv[0] << 24) | ((uint32_t)iv[1] << 16) | ((uint32_t)iv[2] << 8) | iv[3];
}

void* subghz_protocol_encoder_renault_v1_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderRenaultV1* instance = malloc(sizeof(SubGhzProtocolEncoderRenaultV1));

    instance->base.protocol = &subghz_protocol_renault_v1;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 1;
    instance->encoder.size_upload = RENAULT_V1_UPLOAD_SIZE;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_renault_v1_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderRenaultV1* instance = context;
    SubGhzProtocolStatus res = SubGhzProtocolStatusError;
    do {
        if(subghz_block_generic_deserialize_check_count_bit(
            &instance->generic, flipper_format, subghz_protocol_renault_v1_const.min_count_bit_for_found) !=
           SubGhzProtocolStatusOk) {

            break;
        }
        if(!flipper_format_rewind(flipper_format)) {

            break;
        }
        if(!subghz_block_read_hex(flipper_format, "Key", &instance->generic.data, sizeof(uint64_t))) {

            break;
        }
        if(!subghz_block_read_hex(
               flipper_format, "Key_2", &instance->generic.data_2, sizeof(uint64_t))) {

            break;
        }

        instance->generic.status = 0;
        instance->generic.seed = 0;
        if(flipper_format_rewind(flipper_format))
            subghz_block_read_hex(
                flipper_format, "Recovered", &instance->generic.status, sizeof(uint8_t));
        if(flipper_format_rewind(flipper_format))
            subghz_block_read_hex(
                flipper_format, "Seed", &instance->generic.seed, sizeof(uint32_t));
        if(!flipper_format_rewind(flipper_format)) break;
        subghz_protocol_renault_v1_check_remote_controller(&instance->generic);

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);
        if(!subghz_protocol_encoder_renault_v1_get_upload(instance)) {
            res = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }

        if(!subghz_block_write_hex(flipper_format, "Key", instance->generic.data, sizeof(uint64_t))) {

            break;
        }
        if(!subghz_block_write_hex(
               flipper_format, "Key_2", instance->generic.data_2, sizeof(uint64_t))) {

            break;
        }

        if(instance->generic.status == 1 &&
           !subghz_block_write_hex(
               flipper_format, "Seed", instance->generic.seed, sizeof(uint32_t))) {
            break;
        }

        instance->encoder.is_running = true;

        res = SubGhzProtocolStatusOk;
    } while(false);

    return res;
}

static const SubGhzProtocolEncoder enc_renault_v1_vt = {
    .alloc = subghz_protocol_encoder_renault_v1_alloc,
    .free = subghz_protocol_encoder_generic_free,
    .deserialize = subghz_protocol_encoder_renault_v1_deserialize,
    .stop = subghz_protocol_encoder_generic_stop,
    .yield = subghz_protocol_encoder_generic_yield,
};

const SubGhzProtocol subghz_protocol_renault_v1 = {
    .name = SUBGHZ_PROTOCOL_RENAULT_V1_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 | SubGhzProtocolFlag_315 |
            SubGhzProtocolFlag_FM | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send |
            SubGhzProtocolFlag_Bruteforce,
    .decoder = NULL,
    .encoder = &enc_renault_v1_vt,
};

static const SubGhzEncodePluginEntry enc_renault_v1_entry = {.name = SUBGHZ_PROTOCOL_RENAULT_V1_NAME, .encoder = &enc_renault_v1_vt, .host_slot = &enc_renault_v1_host_raw};
static const FlipperAppPluginDescriptor enc_renault_v1_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_renault_v1_entry};
const FlipperAppPluginDescriptor* enc_renault_v1_ep(void) { return &enc_renault_v1_descriptor; }
