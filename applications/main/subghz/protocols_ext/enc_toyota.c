/* enc_toyota.fal : SD encoder for toyota (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_toyota_alloc enc_toyota__subghz_protocol_encoder_toyota_alloc
#define subghz_protocol_encoder_toyota_deserialize enc_toyota__subghz_protocol_encoder_toyota_deserialize
#define subghz_protocol_encoder_toyota_free enc_toyota__subghz_protocol_encoder_toyota_free
#define subghz_protocol_encoder_toyota_stop enc_toyota__subghz_protocol_encoder_toyota_stop
#define subghz_protocol_encoder_toyota_yield enc_toyota__subghz_protocol_encoder_toyota_yield
#define subghz_protocol_toyota enc_toyota__subghz_protocol_toyota
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/toyota.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolToyota"
#define TOYOTA_B_SYNC_GAP_MIN   1700u
#define TOYOTA_B_SYNC_GAP_MAX   2200u
#define TOYOTA_B_PREAMBLE_MIN   10u
#define TOYOTA_C_BITS           66u
#define TOYOTA_C_FRAME_BITS     68u
#define TOYOTA_C_PREAMBLE_MIN   6u
#define TOYOTA_C_SYNC_MIN       1050u
#define TOYOTA_C_SYNC_MAX       1500u
#define TOYOTA_C_END_LOW        1000u
#define TOYOTA_ENC_UPLOAD_SIZE 256
#define TOYOTA_C_TX_PRE_HIGH  375u
#define TOYOTA_C_TX_PRE_LOW   425u
#define TOYOTA_C_TX_SEP_LOW   425u
#define TOYOTA_C_TX_BIT1_HIGH 800u
#define TOYOTA_C_TX_BIT0_HIGH 400u

typedef struct {
    SubGhzProtocolEncoderBase  base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric         generic;
} SubGhzProtocolEncoderToyota;

static const SubGhzBlockConst toyota_const_b = {
    .te_short                = 200,
    .te_long                 = 390,
    .te_delta                = 60,
    .min_count_bit_for_found = 67,
};

/* Frame bit j (0 = first on air). Bits 0..63 come from data (MSB first), the
 * remainder from data_2 (MSB first over its bit_count-64 bits). */
static inline uint8_t toyota_frame_bit(uint64_t data, uint64_t data_2, uint8_t bit_count, uint8_t j);
static bool subghz_protocol_encoder_toyota_get_upload(SubGhzProtocolEncoderToyota* inst);

/* Frame bit j (0 = first on air). Bits 0..63 come from data (MSB first), the
 * remainder from data_2 (MSB first over its bit_count-64 bits). */
static inline uint8_t toyota_frame_bit(uint64_t data, uint64_t data_2, uint8_t bit_count, uint8_t j) {
    if(j < 64) return (uint8_t)((data >> (63 - j)) & 1U);
    uint8_t tb = (uint8_t)(bit_count - 64);
    uint8_t k  = (uint8_t)(j - 64);               /* 0..tb-1 */
    return (uint8_t)((data_2 >> (tb - 1 - k)) & 1U);
}

static bool subghz_protocol_encoder_toyota_get_upload(SubGhzProtocolEncoderToyota* inst) {
    furi_assert(inst);
    const uint8_t  bit_count = inst->generic.data_count_bit;
    const uint64_t data      = inst->generic.data;
    const uint64_t data_2    = inst->generic.data_2;
    /* cnt carries the decoder's variant: 2 = C, otherwise B (A is unreachable) */
    const bool     variant_c = (inst->generic.cnt == 2);

    if(bit_count < 64 || bit_count > TOYOTA_C_FRAME_BITS) {
        FURI_LOG_E(TAG, "Unsupported bit count %u", bit_count);
        return false;
    }

    LevelDuration* up = inst->encoder.upload;
    size_t idx = 0;
    const size_t cap = TOYOTA_ENC_UPLOAD_SIZE;

    if(variant_c) {
        for(uint8_t i = 0; i < TOYOTA_C_PREAMBLE_MIN + 2 && idx + 2 <= cap; i++) {
            up[idx++] = level_duration_make(true,  TOYOTA_C_TX_PRE_HIGH);
            up[idx++] = level_duration_make(false, TOYOTA_C_TX_PRE_LOW);
        }
        up[idx++] = level_duration_make(true,  TOYOTA_C_TX_PRE_HIGH);
        up[idx++] = level_duration_make(false, (TOYOTA_C_SYNC_MIN + TOYOTA_C_SYNC_MAX) / 2);
        for(uint8_t j = 0; j < bit_count && idx + 2 <= cap; j++) {
            uint8_t b = toyota_frame_bit(data, data_2, bit_count, j);
            up[idx++] = level_duration_make(true,  b ? TOYOTA_C_TX_BIT1_HIGH : TOYOTA_C_TX_BIT0_HIGH);
            up[idx++] = level_duration_make(false, TOYOTA_C_TX_SEP_LOW);
        }
        if(idx < cap) up[idx++] = level_duration_make(false, TOYOTA_C_END_LOW + 200);
    } else {
        const SubGhzBlockConst* c = &toyota_const_b;
        for(uint8_t i = 0; i < TOYOTA_B_PREAMBLE_MIN + 2 && idx + 2 <= cap; i++) {
            up[idx++] = level_duration_make(true,  c->te_short);
            up[idx++] = level_duration_make(false, c->te_long);
        }
        up[idx++] = level_duration_make(true,  c->te_short);
        up[idx++] = level_duration_make(false, (TOYOTA_B_SYNC_GAP_MIN + TOYOTA_B_SYNC_GAP_MAX) / 2);
        /* NRZ: one pulse per bit, alternating level; width encodes the bit */
        bool lvl = true;
        for(uint8_t j = 0; j < bit_count && idx < cap; j++) {
            uint8_t b = toyota_frame_bit(data, data_2, bit_count, j);
            up[idx++] = level_duration_make(lvl, b ? c->te_long : c->te_short);
            lvl = !lvl;
        }
        if(idx < cap) up[idx++] = level_duration_make(!lvl, TOYOTA_B_SYNC_GAP_MIN + 100);
    }

    inst->encoder.size_upload = idx;
    return true;
}

void* subghz_protocol_encoder_toyota_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderToyota* inst = malloc(sizeof(SubGhzProtocolEncoderToyota));
    inst->base.protocol         = &subghz_protocol_toyota;
    inst->generic.protocol_name = inst->base.protocol->name;
    inst->encoder.repeat        = 5;
    inst->encoder.size_upload   = TOYOTA_ENC_UPLOAD_SIZE;
    inst->encoder.upload        = malloc(TOYOTA_ENC_UPLOAD_SIZE * sizeof(LevelDuration));
    inst->encoder.front         = 0;
    inst->encoder.is_running    = false;
    return inst;
}

void subghz_protocol_encoder_toyota_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderToyota* inst = context;
    free(inst->encoder.upload);
    free(inst);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_toyota_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderToyota* inst = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize(&inst->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) break;
        if(inst->generic.data_count_bit < TOYOTA_C_BITS) {
            FURI_LOG_E(TAG, "Wrong number of bits in key");
            ret = SubGhzProtocolStatusErrorValueBitCount;
            break;
        }
        /* Optional trailing bits + optional Repeat */
        uint32_t tail = 0;
        if(!flipper_format_rewind(flipper_format) ||
           !flipper_format_read_uint32(flipper_format, "Tail", &tail, 1)) {
            tail = 0;
        }
        inst->generic.data_2 = tail;
        flipper_format_rewind(flipper_format);
        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&inst->encoder.repeat, 1);
        flipper_format_rewind(flipper_format);

        if(!subghz_protocol_encoder_toyota_get_upload(inst)) {
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }
        inst->encoder.front      = 0;
        inst->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);
    return ret;
}

void subghz_protocol_encoder_toyota_stop(void* context) {
    SubGhzProtocolEncoderToyota* inst = context;
    inst->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_toyota_yield(void* context) {
    SubGhzProtocolEncoderToyota* inst = context;
    if(inst->encoder.repeat == 0 || !inst->encoder.is_running) {
        inst->encoder.is_running = false;
        return level_duration_reset();
    }
    LevelDuration ret = inst->encoder.upload[inst->encoder.front];
    if(++inst->encoder.front == inst->encoder.size_upload) {
        if(!subghz_block_generic_global.endless_tx) inst->encoder.repeat--;
        inst->encoder.front = 0;
    }
    return ret;
}

static const SubGhzProtocolEncoder enc_toyota_vt = {
    .alloc = subghz_protocol_encoder_toyota_alloc,
    .free = subghz_protocol_encoder_toyota_free,
    .deserialize = subghz_protocol_encoder_toyota_deserialize,
    .stop = subghz_protocol_encoder_toyota_stop,
    .yield = subghz_protocol_encoder_toyota_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_toyota = {
    .name = SUBGHZ_PROTOCOL_TOYOTA_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 |
               SubGhzProtocolFlag_315 |
               SubGhzProtocolFlag_AM  |
               SubGhzProtocolFlag_Decodable |
               SubGhzProtocolFlag_Load |
               SubGhzProtocolFlag_Save |
               SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_toyota_vt,
};

static const SubGhzEncodePluginEntry enc_toyota_entry = {.name = SUBGHZ_PROTOCOL_TOYOTA_NAME, .encoder = &enc_toyota_vt};
static const FlipperAppPluginDescriptor enc_toyota_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_toyota_entry};
const FlipperAppPluginDescriptor* enc_toyota_ep(void) { return &enc_toyota_descriptor; }
