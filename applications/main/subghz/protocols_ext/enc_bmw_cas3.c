#define subghz_protocol_bmw_cas3 enc_bmw_cas3__subghz_protocol_bmw_cas3
#define subghz_protocol_encoder_bmw_cas3_alloc enc_bmw_cas3__subghz_protocol_encoder_bmw_cas3_alloc
#define subghz_protocol_encoder_bmw_cas3_deserialize enc_bmw_cas3__subghz_protocol_encoder_bmw_cas3_deserialize
#define subghz_protocol_encoder_common_free enc_bmw_cas3__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_bmw_cas3__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_bmw_cas3__subghz_protocol_encoder_common_yield
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/bmw_cas3.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/protocols/common.h>

#define BMW_CAS3_REPEAT      3u
#define BMW_CAS3_UPLOAD_SIZE (BMW_CAS3_MAX_BITS * 2u + 4u)

struct SubGhzProtocolEncoderBmwCas3 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

void subghz_protocol_encoder_common_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderCommon* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

void subghz_protocol_encoder_common_stop(void* context) {
    SubGhzProtocolEncoderCommon* instance = context;
    instance->encoder.is_running = false;
}

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

void* subghz_protocol_encoder_bmw_cas3_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderBmwCas3* instance = calloc(1, sizeof(SubGhzProtocolEncoderBmwCas3));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_bmw_cas3;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = BMW_CAS3_REPEAT;
    instance->encoder.upload = malloc(BMW_CAS3_UPLOAD_SIZE * sizeof(LevelDuration));
    furi_check(instance->encoder.upload);
    instance->encoder.size_upload = 0;
    instance->encoder.is_running = false;
    return instance;
}

static bool bmw_cas3_bit(const SubGhzBlockGeneric* generic, uint8_t i) {
    if(i < 64) {
        uint8_t first = generic->data_count_bit < 64 ? generic->data_count_bit : 64;
        return (generic->data >> (first - 1 - i)) & 1;
    }
    uint8_t rest = generic->data_count_bit - 64;
    return (generic->data_2 >> (rest - 1 - (i - 64))) & 1;
}

static void bmw_cas3_build_upload(SubGhzProtocolEncoderBmwCas3* instance) {
    size_t index = 0;
    LevelDuration* upload = instance->encoder.upload;
    upload[index++] = level_duration_make(true, BMW_CAS3_SYNC_HIGH_US);
    upload[index++] = level_duration_make(false, BMW_CAS3_SYNC_LOW_US);
    for(uint8_t i = 0; i < instance->generic.data_count_bit; i++) {
        upload[index++] = level_duration_make(true, BMW_CAS3_MARK_US);
        upload[index++] = level_duration_make(
            false, bmw_cas3_bit(&instance->generic, i) ? BMW_CAS3_SPACE_1_US : BMW_CAS3_SPACE_0_US);
    }
    upload[index++] = level_duration_make(true, BMW_CAS3_TAIL_HIGH_US);
    upload[index++] = level_duration_make(false, BMW_CAS3_TAIL_LOW_US);
    instance->encoder.size_upload = index;
    instance->encoder.front = 0;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_bmw_cas3_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderBmwCas3* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
    if(ret != SubGhzProtocolStatusOk) return ret;
    if(instance->generic.data_count_bit < BMW_CAS3_MIN_BITS ||
       instance->generic.data_count_bit > BMW_CAS3_MAX_BITS) {
        return SubGhzProtocolStatusErrorParserBitCount;
    }
    instance->generic.data_2 = 0;
    if(instance->generic.data_count_bit > 64) {
        uint8_t key_data[sizeof(uint64_t)] = {0};
        if(!flipper_format_rewind(flipper_format) ||
           !flipper_format_read_hex(flipper_format, "Data", key_data, sizeof(uint64_t))) {
            return SubGhzProtocolStatusErrorParserOthers;
        }
        for(uint8_t i = 0; i < sizeof(uint64_t); i++) {
            instance->generic.data_2 = (instance->generic.data_2 << 8) | key_data[i];
        }
    }
    flipper_format_rewind(flipper_format);
    uint32_t repeat = BMW_CAS3_REPEAT;
    if(flipper_format_read_uint32(flipper_format, "Repeat", &repeat, 1) && repeat > 0) {
        instance->encoder.repeat = repeat;
    }
    bmw_cas3_build_upload(instance);
    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

static const SubGhzProtocolEncoder enc_bmw_cas3_vt = {
    .alloc = subghz_protocol_encoder_bmw_cas3_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_bmw_cas3_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

const SubGhzProtocol subghz_protocol_bmw_cas3 = {
    .name = BMW_CAS3_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 |
            SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_bmw_cas3_vt,
};

static const SubGhzEncodePluginEntry enc_bmw_cas3_entry = {
    .name = BMW_CAS3_PROTOCOL_NAME,
    .encoder = &enc_bmw_cas3_vt};
static const FlipperAppPluginDescriptor enc_bmw_cas3_descriptor = {
    .appid = SUBGHZ_ENCODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION,
    .entry_point = &enc_bmw_cas3_entry};
const FlipperAppPluginDescriptor* enc_bmw_cas3_ep(void) {
    return &enc_bmw_cas3_descriptor;
}
