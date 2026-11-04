/* enc_porsche_touareg.fal : SD encoder for porsche_touareg (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define porsche_touareg_protocol enc_porsche_touareg__porsche_touareg_protocol
#define subghz_protocol_encoder_porsche_touareg_alloc enc_porsche_touareg__subghz_protocol_encoder_porsche_touareg_alloc
#define subghz_protocol_encoder_porsche_touareg_deserialize enc_porsche_touareg__subghz_protocol_encoder_porsche_touareg_deserialize
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/porsche_touareg.h>
#include <furi.h>
#include <string.h>
#include <lib/subghz/types.h>
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>
#include <flipper_format/flipper_format.h>
#include <string.h>
#include <lib/subghz/protocols/pp_auto_common.h>
#include <lib/subghz/protocols/common.h>

#define FF_KEY                    "Key"
#define PORSCHE_TOUAREG_BIT_COUNT 64
#define PC_TE_SYNC                3370U
#define PC_TE_GAP                 5930U
#define PORSCHE_TOUAREG_SYNC_TX 20U
#define PORSCHE_TOUAREG_UPLOAD_CAPACITY \
    ((PORSCHE_TOUAREG_SYNC_TX * 2U) + 2U + (PORSCHE_TOUAREG_BIT_COUNT * 2U))
#define ROTATE24(rh, rm, rl)                 \
    do {                                     \
        uint8_t _ch = ((rh) >> 7) & 1U;      \
        uint8_t _cm = ((rm) >> 7) & 1U;      \
        uint8_t _cl = ((rl) >> 7) & 1U;      \
        (rh) = (uint8_t)(((rh) << 1) | _cm); \
        (rm) = (uint8_t)(((rm) << 1) | _cl); \
        (rl) = (uint8_t)(((rl) << 1) | _ch); \
    } while(0)

typedef struct SubGhzProtocolEncoderPorscheTouareg {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint32_t serial;
    uint8_t btn;
    uint16_t cnt;
    uint8_t frame_type;
} SubGhzProtocolEncoderPorscheTouareg;

static const SubGhzBlockConst subghz_protocol_porsche_touareg_const = {
    .te_short = 1680,
    .te_long = 3370,
    .te_delta = 500,
    .min_count_bit_for_found = PORSCHE_TOUAREG_BIT_COUNT,
};

/* host bridge: live firmware functions/state this encoder needs (set by the loader) */
typedef struct {
    __typeof__(&ppa_bytes_to_u64_be) ppa_bytes_to_u64_be;
    __typeof__(&ppa_emit) ppa_emit;
    __typeof__(&ppa_encoder_buffer_ensure) ppa_encoder_buffer_ensure;
    __typeof__(&ppa_encoder_free) ppa_encoder_free;
    __typeof__(&ppa_encoder_read_fields) ppa_encoder_read_fields;
    __typeof__(&ppa_encoder_read_repeat) ppa_encoder_read_repeat;
    __typeof__(&ppa_encoder_stop) ppa_encoder_stop;
    __typeof__(&ppa_encoder_yield) ppa_encoder_yield;
    __typeof__(&ppa_u64_to_bytes_be) ppa_u64_to_bytes_be;
    __typeof__(&ppa_verify_protocol_name) ppa_verify_protocol_name;
} enc_porsche_touareg_host_t;
static const void* enc_porsche_touareg_host_raw;
#define ENC_PORSCHE_TOUAREG_HOST ((const enc_porsche_touareg_host_t*)enc_porsche_touareg_host_raw)
#define ppa_bytes_to_u64_be (ENC_PORSCHE_TOUAREG_HOST->ppa_bytes_to_u64_be)
#define ppa_emit (ENC_PORSCHE_TOUAREG_HOST->ppa_emit)
#define ppa_encoder_buffer_ensure (ENC_PORSCHE_TOUAREG_HOST->ppa_encoder_buffer_ensure)
#define ppa_encoder_free (ENC_PORSCHE_TOUAREG_HOST->ppa_encoder_free)
#define ppa_encoder_read_fields (ENC_PORSCHE_TOUAREG_HOST->ppa_encoder_read_fields)
#define ppa_encoder_read_repeat (ENC_PORSCHE_TOUAREG_HOST->ppa_encoder_read_repeat)
#define ppa_encoder_stop (ENC_PORSCHE_TOUAREG_HOST->ppa_encoder_stop)
#define ppa_encoder_yield (ENC_PORSCHE_TOUAREG_HOST->ppa_encoder_yield)
#define ppa_u64_to_bytes_be (ENC_PORSCHE_TOUAREG_HOST->ppa_u64_to_bytes_be)
#define ppa_verify_protocol_name (ENC_PORSCHE_TOUAREG_HOST->ppa_verify_protocol_name)

static void porsche_touareg_compute_frame(
    uint32_t serial24,
    uint8_t btn,
    uint16_t counter,
    uint8_t frame_type,
    uint8_t* pkt);
static bool porsche_touareg_build_upload(SubGhzProtocolEncoderPorscheTouareg* instance);

static void porsche_touareg_compute_frame(
    uint32_t serial24,
    uint8_t btn,
    uint16_t counter,
    uint8_t frame_type,
    uint8_t* pkt) {
    uint8_t b0 = (uint8_t)((btn << 4) | (frame_type & 0x07));
    uint8_t b1 = (serial24 >> 16) & 0xFF;
    uint8_t b2 = (serial24 >> 8) & 0xFF;
    uint8_t b3 = serial24 & 0xFF;

    uint16_t cnt = counter + 1;
    uint8_t cnt_lo = cnt & 0xFF;
    uint8_t cnt_hi = (cnt >> 8) & 0xFF;

    uint8_t r_h = b3;
    uint8_t r_m = b1;
    uint8_t r_l = b2;

#define ROTATE24(rh, rm, rl)                 \
    do {                                     \
        uint8_t _ch = ((rh) >> 7) & 1U;      \
        uint8_t _cm = ((rm) >> 7) & 1U;      \
        uint8_t _cl = ((rl) >> 7) & 1U;      \
        (rh) = (uint8_t)(((rh) << 1) | _cm); \
        (rm) = (uint8_t)(((rm) << 1) | _cl); \
        (rl) = (uint8_t)(((rl) << 1) | _ch); \
    } while(0)

    for(uint8_t i = 0; i < 4; i++) {
        ROTATE24(r_h, r_m, r_l);
    }
    for(uint16_t i = 0; i < cnt_lo; i++) {
        ROTATE24(r_h, r_m, r_l);
    }

#undef ROTATE24

    uint8_t a9a = r_h ^ b0;

    uint8_t nb9b_p1 = (uint8_t)((~cnt_lo << 2) & 0xFC) ^ r_m;
    uint8_t nb9b_p2 = (uint8_t)((~cnt_hi << 2) & 0xFC) ^ r_m;
    uint8_t nb9b_p3 = (uint8_t)((~cnt_hi >> 6) & 0x03) ^ r_m;
    uint8_t a9b = (nb9b_p1 & 0xCC) | (nb9b_p2 & 0x30) | (nb9b_p3 & 0x03);

    uint8_t nb9c_p1 = (uint8_t)((~cnt_lo >> 2) & 0x3F) ^ r_l;
    uint8_t nb9c_p2 = (uint8_t)((~cnt_hi & 0x03) << 6) ^ r_l;
    uint8_t nb9c_p3 = (uint8_t)((~cnt_hi >> 2) & 0x3F) ^ r_l;
    uint8_t a9c = (nb9c_p1 & 0x33) | (nb9c_p2 & 0xC0) | (nb9c_p3 & 0x0C);

    pkt[0] = b0;
    pkt[1] = b1;
    pkt[2] = b2;
    pkt[3] = b3;
    pkt[4] = (uint8_t)(((a9a >> 2) & 0x3F) | ((~cnt_lo & 0x03U) << 6));
    pkt[5] = (uint8_t)((~cnt_lo & 0xC0U) | ((a9a & 0x03U) << 4) | (a9b & 0x0CU) |
                       ((~cnt_lo >> 2) & 0x03U));
    pkt[6] = (uint8_t)(((a9b & 0x03U) << 6) | ((a9c >> 2) & 0x3CU) | ((~cnt_lo >> 4) & 0x03U));
    pkt[7] = (uint8_t)(((a9b >> 4) & 0x0FU) | ((a9c & 0x0FU) << 4));
}

static bool porsche_touareg_build_upload(SubGhzProtocolEncoderPorscheTouareg* instance) {
    furi_check(instance);

    LevelDuration* up = instance->encoder.upload;
    const size_t cap = PORSCHE_TOUAREG_UPLOAD_CAPACITY;
    const uint32_t te_short = subghz_protocol_porsche_touareg_const.te_short;
    const uint32_t te_long = subghz_protocol_porsche_touareg_const.te_long;
    size_t index = 0;

    // Sync train: alternating off/on pulses of PC_TE_SYNC
    for(size_t p = 0; p < PORSCHE_TOUAREG_SYNC_TX; p++) {
        index = ppa_emit(up, index, cap, false, PC_TE_SYNC);
        index = ppa_emit(up, index, cap, true, PC_TE_SYNC);
    }

    // Gap: one off then one on pulse of PC_TE_GAP
    index = ppa_emit(up, index, cap, false, PC_TE_GAP);
    index = ppa_emit(up, index, cap, true, PC_TE_GAP);

    // Data: 64 bits MSB-first, PWM off+on pair per bit
    const uint64_t data = instance->generic.data;
    for(int8_t b = PORSCHE_TOUAREG_BIT_COUNT - 1; b >= 0; b--) {
        const bool bit_value = (data >> b) & 1U;
        if(bit_value) {
            index = ppa_emit(up, index, cap, false, te_long);
            index = ppa_emit(up, index, cap, true, te_short);
        } else {
            index = ppa_emit(up, index, cap, false, te_short);
            index = ppa_emit(up, index, cap, true, te_long);
        }
    }

    if(index > cap) {
        return false;
    }

    instance->encoder.front = 0;
    instance->encoder.size_upload = index;
    return true;
}

void* subghz_protocol_encoder_porsche_touareg_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);

    SubGhzProtocolEncoderPorscheTouareg* instance =
        calloc(1, sizeof(SubGhzProtocolEncoderPorscheTouareg));
    furi_check(instance);

    instance->base.protocol = &porsche_touareg_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = 10;
    instance->encoder.front = 0;
    instance->encoder.is_running = false;
    ppa_encoder_buffer_ensure(instance, PORSCHE_TOUAREG_UPLOAD_CAPACITY);

    return instance;
}

SubGhzProtocolStatus subghz_protocol_encoder_porsche_touareg_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderPorscheTouareg* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    instance->encoder.is_running = false;
    instance->encoder.front = 0;

    do {
        if(ppa_verify_protocol_name(flipper_format, instance->base.protocol->name) !=
           SubGhzProtocolStatusOk) {
            break;
        }

        flipper_format_rewind(flipper_format);
        SubGhzProtocolStatus load_st = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_porsche_touareg_const.min_count_bit_for_found);
        if(load_st != SubGhzProtocolStatusOk) {
            break;
        }

        // The saved Key already holds the exact 64-bit on-air frame. Recover the
        // fields it encodes so an edited counter/serial/button can be re-rolled.
        const uint8_t pkt0 = (uint8_t)((instance->generic.data >> 56) & 0xFF);
        instance->frame_type = pkt0 & 0x07;

        uint32_t serial = (uint32_t)((instance->generic.data >> 32) & 0xFFFFFFU);
        uint32_t btn = (uint32_t)(pkt0 >> 4);
        uint32_t cnt = instance->generic.cnt;
        ppa_encoder_read_fields(flipper_format, &serial, &btn, &cnt, NULL);

        instance->serial = serial & 0xFFFFFFU;
        instance->btn = (uint8_t)(btn & 0x0FU);
        instance->cnt = (uint16_t)cnt;

        instance->generic.serial = instance->serial;
        instance->generic.btn = instance->btn;
        instance->generic.cnt = instance->cnt;

        instance->encoder.repeat = ppa_encoder_read_repeat(flipper_format, 10);

        // Regenerate the frame from the fields via the same forward transform the
        // decoder inverts. For an unmodified capture this reproduces the loaded Key
        // byte-for-byte; if the counter was recovered as 0 we fall back to a raw
        // replay of the loaded frame.
        if(instance->cnt >= 1) {
            uint8_t pkt[8];
            porsche_touareg_compute_frame(
                instance->serial,
                instance->btn,
                (uint16_t)(instance->cnt - 1),
                instance->frame_type,
                pkt);
            instance->generic.data = ppa_bytes_to_u64_be(pkt);
        }
        instance->generic.data_count_bit = PORSCHE_TOUAREG_BIT_COUNT;

        if(!porsche_touareg_build_upload(instance)) {
            break;
        }
        if(instance->encoder.size_upload == 0) {
            break;
        }

        flipper_format_rewind(flipper_format);
        uint8_t key_data[sizeof(uint64_t)];
        ppa_u64_to_bytes_be(instance->generic.data, key_data);
        if(!flipper_format_update_hex(flipper_format, FF_KEY, key_data, sizeof(key_data))) {
            break;
        }

        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

static void enc_porsche_touareg_tr_free(void* context) {
    ppa_encoder_free(context);
}

static void enc_porsche_touareg_tr_stop(void* context) {
    ppa_encoder_stop(context);
}

static LevelDuration enc_porsche_touareg_tr_yield(void* context) {
    return ppa_encoder_yield(context);
}

static const SubGhzProtocolEncoder enc_porsche_touareg_vt = {
    .alloc = subghz_protocol_encoder_porsche_touareg_alloc,
    .free = enc_porsche_touareg_tr_free,
    .deserialize = subghz_protocol_encoder_porsche_touareg_deserialize,
    .stop = enc_porsche_touareg_tr_stop,
    .yield = enc_porsche_touareg_tr_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol porsche_touareg_protocol = {
    .name = PORSCHE_TOUAREG_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_porsche_touareg_vt,
};

static const SubGhzEncodePluginEntry enc_porsche_touareg_entry = {.name = PORSCHE_TOUAREG_PROTOCOL_NAME, .encoder = &enc_porsche_touareg_vt, .host_slot = &enc_porsche_touareg_host_raw};
static const FlipperAppPluginDescriptor enc_porsche_touareg_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_porsche_touareg_entry};
const FlipperAppPluginDescriptor* enc_porsche_touareg_ep(void) { return &enc_porsche_touareg_descriptor; }
