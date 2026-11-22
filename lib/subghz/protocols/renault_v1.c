
#include "renault_v1.h"

#include "common.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"
#include "renault_v1_crypto.h"
#include <lib/toolbox/manchester_decoder.h>

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#include "../blocks/custom_btn_i.h"

#define TAG "RenaultV1"

static const SubGhzBlockConst subghz_protocol_renault_v1_const = {
    .te_short = 125,
    .te_long = 250,
    .te_delta = 100,
    .min_count_bit_for_found = 88,
};

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
#define RENAULT_V1_PULSE_MIN       50
#define RENAULT_V1_PULSE_MAX \
    520

#define RENAULT_V1_TE_SEED_HI 120
#define RENAULT_V1_TE_SEED_LO 150
#define RENAULT_V1_TE_TRACK_MIN \
    70
#define RENAULT_V1_TE_TRACK_MAX    185
#define RENAULT_V1_THR_MIN         150
#define RENAULT_V1_THR_MAX         210

#define RENAULT_V1_GAP_LO_MIN      1150
#define RENAULT_V1_GAP_LO_MAX      2200
#define RENAULT_V1_GAP_HI_MIN      800
#define RENAULT_V1_GAP_HI_MAX      1150

enum {
    RENAULT_V1_DATA_FRAME_ENTRIES =
        2 + (RENAULT_V1_SYNC_BIT_COUNT + RENAULT_V1_DATA_1_SIZE + RENAULT_V1_DATA_2_SIZE) * 2,
    RENAULT_V1_TAIL_FRAME_ENTRIES =
        2 + (RENAULT_V1_SYNC_BIT_COUNT + RENAULT_V1_CHECK_BIT_COUNT) * 2 + 1,
    RENAULT_V1_UPLOAD_SIZE = RENAULT_V1_PRE_COUNT * 2 +
                             RENAULT_V1_DATA_REPEAT * RENAULT_V1_DATA_FRAME_ENTRIES +
                             RENAULT_V1_DATA_REPEAT * RENAULT_V1_TAIL_FRAME_ENTRIES,
};

struct SubGhzProtocolDecoderRenaultV1 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_saved_state;

    uint16_t sync;
    uint16_t te_hi;
    uint16_t te_lo;
};
SUBGHZ_ASSERT_DECODER_PREFIX(SubGhzProtocolDecoderRenaultV1);

struct SubGhzProtocolEncoderRenaultV1 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderRenaultV1);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderRenaultV1);

SUBGHZ_ASSERT_ENCODER_PREFIX(SubGhzProtocolEncoderRenaultV1);

typedef enum {
    RenaultV1DecoderStepReset = 0,
    RenaultV1DecoderStepCheckPreamble,
    RenaultV1DecoderStepCheckSync,
    RenaultV1DecoderStepDecodeData,
} RenaultV1DecoderStep;

const SubGhzProtocolDecoder subghz_protocol_renault_v1_decoder = {
    .alloc = subghz_protocol_decoder_renault_v1_alloc,
    .free = subghz_protocol_decoder_generic_free,

    .feed = subghz_protocol_decoder_renault_v1_feed,
    .reset = subghz_protocol_decoder_renault_v1_reset,

    .get_hash_data = subghz_protocol_decoder_renault_v1_get_hash_data,
    .serialize = subghz_protocol_decoder_renault_v1_serialize,
    .deserialize = subghz_protocol_decoder_renault_v1_deserialize,
    .get_string = subghz_protocol_decoder_renault_v1_get_string,
};

typedef struct {
    __typeof__(&subghz_protocol_renault_v1_encrypt) subghz_protocol_renault_v1_encrypt;
} enc_renault_v1_host_t;
static const enc_renault_v1_host_t enc_renault_v1_host = {
    .subghz_protocol_renault_v1_encrypt = subghz_protocol_renault_v1_encrypt,
};
static const SubGhzEncoderExternal subghz_protocol_renault_v1_encoder_ext = SUBGHZ_ENCODER_EXTERNAL_HOST("enc_renault_v1", &enc_renault_v1_host);

const SubGhzProtocol subghz_protocol_renault_v1 = {
    .name = SUBGHZ_PROTOCOL_RENAULT_V1_NAME,

    .tx_heap_budget = 10752,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 | SubGhzProtocolFlag_315 |
            SubGhzProtocolFlag_FM | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send |
            SubGhzProtocolFlag_Bruteforce,

    .decoder = &subghz_protocol_renault_v1_decoder,
    .encoder = &subghz_protocol_renault_v1_encoder_ext.vtable,
};

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

uint32_t renault_v1_iv_to_u32(const uint8_t iv[4]) {
    return ((uint32_t)iv[0] << 24) | ((uint32_t)iv[1] << 16) | ((uint32_t)iv[2] << 8) | iv[3];
}

void* subghz_protocol_decoder_renault_v1_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderRenaultV1), &subghz_protocol_renault_v1);
}

void subghz_protocol_decoder_renault_v1_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderRenaultV1* instance = context;
    instance->decoder.parser_step = RenaultV1DecoderStepReset;
    manchester_advance(
        instance->manchester_saved_state,
        ManchesterEventReset,
        &instance->manchester_saved_state,
        NULL);
}

static void renault_v1_pack_payload(uint64_t data, uint64_t data_2, uint8_t out[11]) {
    for(int i = 0; i < 8; i++)
        out[i] = (uint8_t)(data >> (56 - 8 * i));
    for(int i = 0; i < 3; i++)
        out[8 + i] = (uint8_t)(data_2 >> (16 - 8 * i));
}

static bool renault_v1_payload_crc_ok(uint64_t data, uint64_t data_2) {
    uint8_t p[11];
    renault_v1_pack_payload(data, data_2, p);
    uint8_t crc = 0;
    for(int i = 0; i < 10; i++)
        crc ^= p[i];
    return crc == p[10];
}

void subghz_protocol_decoder_renault_v1_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderRenaultV1* instance = context;

    switch(instance->decoder.parser_step) {
    case RenaultV1DecoderStepReset:

        if((!level) && (duration >= RENAULT_V1_GAP_LO_MIN) &&
           (duration <= RENAULT_V1_GAP_LO_MAX)) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = RenaultV1DecoderStepCheckSync;
        }
        break;

    case RenaultV1DecoderStepCheckSync:

        if(level && (duration >= RENAULT_V1_GAP_HI_MIN) && (duration <= RENAULT_V1_GAP_HI_MAX) &&
           (instance->decoder.te_last >= RENAULT_V1_GAP_LO_MIN) &&
           (instance->decoder.te_last <= RENAULT_V1_GAP_LO_MAX)) {
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;

            instance->manchester_saved_state = ManchesterStateStart1;
            instance->te_hi = RENAULT_V1_TE_SEED_HI;
            instance->te_lo = RENAULT_V1_TE_SEED_LO;
            instance->decoder.parser_step = RenaultV1DecoderStepDecodeData;
        } else {

            instance->decoder.parser_step = RenaultV1DecoderStepReset;
            if((!level) && (duration >= RENAULT_V1_GAP_LO_MIN) &&
               (duration <= RENAULT_V1_GAP_LO_MAX)) {
                instance->decoder.te_last = duration;
                instance->decoder.parser_step = RenaultV1DecoderStepCheckSync;
            }
        }
        break;

    case RenaultV1DecoderStepDecodeData: {

        if(duration < RENAULT_V1_PULSE_MIN) break;

        if(duration > RENAULT_V1_PULSE_MAX) {

            instance->decoder.parser_step = RenaultV1DecoderStepReset;
            if((!level) && (duration >= RENAULT_V1_GAP_LO_MIN) &&
               (duration <= RENAULT_V1_GAP_LO_MAX)) {
                instance->decoder.te_last = duration;
                instance->decoder.parser_step = RenaultV1DecoderStepCheckSync;
            }
            break;
        }

        uint16_t* te = level ? &instance->te_hi : &instance->te_lo;
        uint32_t thr = ((uint32_t)*te * 3u) / 2u;
        if(thr < RENAULT_V1_THR_MIN) thr = RENAULT_V1_THR_MIN;
        if(thr > RENAULT_V1_THR_MAX) thr = RENAULT_V1_THR_MAX;

        ManchesterEvent event;
        if(duration <= thr) {
            event = level ? ManchesterEventShortHigh : ManchesterEventShortLow;
            uint32_t upd = ((uint32_t)*te * 7u + duration) / 8u;
            if(upd < RENAULT_V1_TE_TRACK_MIN) upd = RENAULT_V1_TE_TRACK_MIN;
            if(upd > RENAULT_V1_TE_TRACK_MAX) upd = RENAULT_V1_TE_TRACK_MAX;
            *te = (uint16_t)upd;
        } else {
            event = level ? ManchesterEventLongHigh : ManchesterEventLongLow;
        }

        bool data;
        if(!manchester_advance(
               instance->manchester_saved_state, event, &instance->manchester_saved_state, &data)) {
            break;
        }
        instance->decoder.decode_data = (instance->decoder.decode_data << 1) | data;
        instance->decoder.decode_count_bit++;

        if(instance->decoder.decode_count_bit == RENAULT_V1_SYNC_BIT_COUNT) {
            instance->sync = ~instance->decoder.decode_data;
            instance->decoder.decode_data = 0;
        } else if(
            instance->decoder.decode_count_bit ==
            (RENAULT_V1_SYNC_BIT_COUNT + RENAULT_V1_DATA_1_SIZE)) {
            instance->generic.data = ~instance->decoder.decode_data;
            instance->decoder.decode_data = 0;
        } else if(
            instance->decoder.decode_count_bit ==
            (RENAULT_V1_SYNC_BIT_COUNT + RENAULT_V1_DATA_1_SIZE + RENAULT_V1_DATA_2_SIZE)) {
            instance->generic.data_2 = (~instance->decoder.decode_data) & 0xFFFFFF;
            instance->generic.data_count_bit = (RENAULT_V1_DATA_1_SIZE + RENAULT_V1_DATA_2_SIZE);

            instance->generic.status = 0;
            instance->generic.seed = 0;

            const uint8_t family = (uint8_t)(instance->generic.data >> 32);
            if(((instance->sync & 0xFFFF) == RENAULT_V1_SYNC_WORD) &&
               ((family & 0xF0) != 0x30) &&
               renault_v1_payload_crc_ok(instance->generic.data, instance->generic.data_2)) {
                if(instance->base.callback)
                    instance->base.callback(&instance->base, instance->base.context);
            }
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = RenaultV1DecoderStepReset;
        }
    } break;
    }
}

static const char* renault_v1_get_name_button(uint8_t btn);

uint8_t subghz_protocol_decoder_renault_v1_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderRenaultV1* instance = context;
    uint8_t hash =
        subghz_protocol_blocks_xor_bytes((uint8_t*)&instance->sync, sizeof(uint16_t));
    hash ^= subghz_protocol_blocks_xor_bytes((uint8_t*)&instance->generic.data, sizeof(uint64_t));
    hash ^=
        subghz_protocol_blocks_xor_bytes((uint8_t*)&instance->generic.data_2, sizeof(uint64_t));
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_renault_v1_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderRenaultV1* instance = context;
    SubGhzProtocolStatus res =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    subghz_protocol_renault_v1_check_remote_controller(&instance->generic);

    if(!subghz_block_write_hex(flipper_format, "Key", instance->generic.data, sizeof(uint64_t))) {

        res = SubGhzProtocolStatusErrorParserOthers;
    }
    if(!subghz_block_write_hex(
           flipper_format, "Key_2", instance->generic.data_2, sizeof(uint64_t))) {

        res = SubGhzProtocolStatusErrorParserOthers;
    }

    uint8_t recovered = (instance->generic.status == 1) ? 1 : 0;
    if(!subghz_block_write_hex(flipper_format, "Recovered", recovered, sizeof(uint8_t))) {
        res = SubGhzProtocolStatusErrorParserOthers;
    }
    if(!subghz_block_write_hex(flipper_format, "Seed", instance->generic.seed, sizeof(uint32_t))) {
        res = SubGhzProtocolStatusErrorParserOthers;
    }

    subghz_block_write_disp(
        flipper_format,
        instance->generic.protocol_name,
        renault_v1_get_name_button(instance->generic.btn));

    return res;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_renault_v1_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderRenaultV1* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_renault_v1_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {

            break;
        }
        if(!flipper_format_rewind(flipper_format)) {

            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        if(!subghz_block_read_hex(flipper_format, "Key", &instance->generic.data, sizeof(uint64_t))) {

            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        if(!subghz_block_read_hex(
               flipper_format, "Key_2", &instance->generic.data_2, sizeof(uint64_t))) {

            ret = SubGhzProtocolStatusErrorParserOthers;
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

        subghz_protocol_renault_v1_check_remote_controller(&instance->generic);
        ret = SubGhzProtocolStatusOk;
    } while(false);
    return ret;
}

static const char* renault_v1_get_name_button(uint8_t btn) {
    switch(btn) {
    case 0x0:
        return "Sync";
    case 0x1:
        return "Lock";
    case 0x2:
        return "Unlock";
    case 0x4:
        return "Trunk";
    case 0x8:
        return "Panic";
    default:
        return "??";
    }
}

void subghz_protocol_decoder_renault_v1_get_string(void* context, FuriString* output) {
    SubGhzProtocolDecoderRenaultV1* instance = context;
    const SubGhzDecodeInput input = {
        .decoder = context,
        .generic = &instance->generic,
        .extra = NULL,
        .extra_count = 0,
        .aux = NULL,
    };
    subghz_decode_plugin_get_string(instance->generic.protocol_name, &input, output);
}

bool subghz_protocol_renault_v1_needs_bruteforce(void* decoder_context) {
    furi_assert(decoder_context);
    SubGhzProtocolDecoderBase* base = decoder_context;
    if(base->protocol != &subghz_protocol_renault_v1) return false;
    SubGhzProtocolDecoderRenaultV1* instance = decoder_context;

    return instance->generic.status == 0;
}

void subghz_protocol_renault_v1_bf_init(RenaultV1BfContext* ctx, void* decoder_context) {
    furi_assert(ctx);
    furi_assert(decoder_context);
    SubGhzProtocolDecoderRenaultV1* instance = decoder_context;
    memset(ctx, 0, sizeof(RenaultV1BfContext));

    renault_v1_pack_payload(instance->generic.data, instance->generic.data_2, ctx->payload);
    ctx->total = RENAULT_V1_BF_RANGE;
    ctx->state = RenaultV1BfStateIdle;
}

void subghz_protocol_renault_v1_bf_apply(RenaultV1BfContext* ctx, void* decoder_context) {
    furi_assert(ctx);
    furi_assert(decoder_context);
    if(ctx->state != RenaultV1BfStateFound) return;
    SubGhzProtocolDecoderRenaultV1* instance = decoder_context;

    instance->generic.seed = renault_v1_iv_to_u32(ctx->iv);
    instance->generic.status = 1;
}

void subghz_protocol_renault_v1_bf_set_not_found(void* decoder_context) {
    furi_assert(decoder_context);
    SubGhzProtocolDecoderBase* base = decoder_context;
    if(base->protocol != &subghz_protocol_renault_v1) return;
    SubGhzProtocolDecoderRenaultV1* instance = decoder_context;
    instance->generic.status = 2;
}
