/* enc_mazda_siemens.fal : SD encoder for mazda_siemens (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_free enc_mazda_siemens__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_mazda_siemens__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_mazda_siemens__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_mazda_siemens_alloc enc_mazda_siemens__subghz_protocol_encoder_mazda_siemens_alloc
#define subghz_protocol_encoder_mazda_siemens_deserialize enc_mazda_siemens__subghz_protocol_encoder_mazda_siemens_deserialize
#define subghz_protocol_mazda_siemens enc_mazda_siemens__subghz_protocol_mazda_siemens
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/mazda_siemens.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolMazdaSiemens"
#define MAZDA_TX_REPEATS 4
#define MAZDA_PREAMBLE_BYTES 12
#define MAZDA_TX_GAP_US 50000
#define MAZDA_UPLOAD_MAX 400

struct SubGhzProtocolEncoderMazdaSiemens {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_mazda_siemens_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 100,
    .min_count_bit_for_found = 64,
};

static uint8_t mazda_byte_parity(uint8_t val);
static void mazda_xor_obfuscate(uint8_t* data);
static size_t mazda_encode_byte(LevelDuration* upload, size_t index, uint8_t byte);
static bool
    subghz_protocol_encoder_mazda_siemens_get_upload(SubGhzProtocolEncoderMazdaSiemens* instance);

static uint8_t mazda_byte_parity(uint8_t val) {
    val ^= val >> 4;
    val ^= val >> 2;
    val ^= val >> 1;
    return val & 1;
}

static void mazda_xor_obfuscate(uint8_t* data) {
    uint8_t old5 = data[5];
    uint8_t old6 = data[6];
    data[5] = (old5 & 0xAA) | (old6 & 0x55);
    data[6] = (old5 & 0x55) | (old6 & 0xAA);

    uint8_t parity = mazda_byte_parity(data[7]);

    if(parity) {
        uint8_t mask = data[6];
        for(int i = 0; i < 6; i++) {
            data[i] ^= mask;
        }
    } else {
        uint8_t mask = data[5];
        for(int i = 0; i < 5; i++) {
            data[i] ^= mask;
        }
        data[6] ^= mask;
    }
}

static size_t mazda_encode_byte(LevelDuration* upload, size_t index, uint8_t byte) {
    uint32_t te = subghz_protocol_mazda_siemens_const.te_short;
    for(int bit = 7; bit >= 0; bit--) {
        if((byte >> bit) & 1) {
            upload[index++] = level_duration_make(true, te);
            upload[index++] = level_duration_make(false, te);
        } else {
            upload[index++] = level_duration_make(false, te);
            upload[index++] = level_duration_make(true, te);
        }
    }
    return index;
}

static bool
    subghz_protocol_encoder_mazda_siemens_get_upload(SubGhzProtocolEncoderMazdaSiemens* instance) {
    furi_assert(instance);

    uint8_t data[8];
    for(int i = 0; i < 8; i++) {
        data[i] = (instance->generic.data >> (56 - 8 * i)) & 0xFF;
    }

    uint8_t cnt_lo = data[6];
    cnt_lo++;
    data[6] = cnt_lo;
    if(cnt_lo == 0) {
        data[5]++;
    }

    uint8_t checksum = 0;
    for(int i = 0; i < 7; i++) {
        checksum += data[i];
    }
    data[7] = checksum;

    uint64_t packed = 0;
    for(int i = 0; i < 8; i++) {
        packed = (packed << 8) | data[i];
    }
    instance->generic.data = packed;

    uint8_t tx_data[8];
    memcpy(tx_data, data, 8);
    mazda_xor_obfuscate(tx_data);

    size_t index = 0;

    for(int i = 0; i < MAZDA_PREAMBLE_BYTES; i++) {
        index = mazda_encode_byte(instance->encoder.upload, index, 0xFF);
    }

    instance->encoder.upload[index++] = level_duration_make(false, MAZDA_TX_GAP_US);

    index = mazda_encode_byte(instance->encoder.upload, index, 0xFF);
    index = mazda_encode_byte(instance->encoder.upload, index, 0xFF);

    index = mazda_encode_byte(instance->encoder.upload, index, 0xD7);

    for(int i = 0; i < 8; i++) {
        index = mazda_encode_byte(instance->encoder.upload, index, 255 - tx_data[i]);
    }

    index = mazda_encode_byte(instance->encoder.upload, index, 0x5A);

    instance->encoder.upload[index++] = level_duration_make(false, MAZDA_TX_GAP_US);

    if(index > MAZDA_UPLOAD_MAX) {
        FURI_LOG_E(TAG, "Upload size %d exceeds buffer %d", (int)index, MAZDA_UPLOAD_MAX);
        return false;
    }

    instance->encoder.size_upload = index;
    return true;
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

void* subghz_protocol_encoder_mazda_siemens_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderMazdaSiemens* instance =
        malloc(sizeof(SubGhzProtocolEncoderMazdaSiemens));

    instance->base.protocol = &subghz_protocol_mazda_siemens;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = MAZDA_TX_REPEATS;
    instance->encoder.size_upload = MAZDA_UPLOAD_MAX;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_mazda_siemens_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderMazdaSiemens* instance = context;
    SubGhzProtocolStatus res = SubGhzProtocolStatusError;
    do {
        res = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_mazda_siemens_const.min_count_bit_for_found);
        if(res != SubGhzProtocolStatusOk) {
            FURI_LOG_E(TAG, "Deserialize error");
            break;
        }
        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        if(!subghz_protocol_encoder_mazda_siemens_get_upload(instance)) {
            res = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }
        instance->encoder.is_running = true;
    } while(false);

    return res;
}

static const SubGhzProtocolEncoder enc_mazda_siemens_vt = {
    .alloc = subghz_protocol_encoder_mazda_siemens_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_mazda_siemens_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_mazda_siemens = {
    .name = SUBGHZ_PROTOCOL_MAZDA_SIEMENS_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_mazda_siemens_vt,
};

static const SubGhzEncodePluginEntry enc_mazda_siemens_entry = {.name = SUBGHZ_PROTOCOL_MAZDA_SIEMENS_NAME, .encoder = &enc_mazda_siemens_vt};
static const FlipperAppPluginDescriptor enc_mazda_siemens_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_mazda_siemens_entry};
const FlipperAppPluginDescriptor* enc_mazda_siemens_ep(void) { return &enc_mazda_siemens_descriptor; }
