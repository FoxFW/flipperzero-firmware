#include "mazda_infinity.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"
#include <furi.h>
#include <furi_hal.h>
#include <string.h>
#include "common.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"

#define MAZDA_INF_TE_SHORT   250U
#define MAZDA_INF_TE_DELTA   62U

#define MAZDA_INF_GAP_US     5000U
#define MAZDA_INF_PREAMBLE_BYTES 12U
#define MAZDA_INF_PAYLOAD_BYTES  8U
#define MAZDA_INF_PAYLOAD_BITS   64U
#define MAZDA_INF_TRAILER        0x5AU

#define MAZDA_INF_SYNC0 0xFFU
#define MAZDA_INF_SYNC1 0xFFU
#define MAZDA_INF_SYNC2 0xD7U

#define MAZDA_INF_BTN_LOCK   0x10U
#define MAZDA_INF_BTN_UNLOCK 0x20U
#define MAZDA_INF_BTN_TRUNK  0x40U

#define MAZDA_INF_DEFAULT_REPEAT 4U

#define MAZDA_INF_WIRE_BYTES (MAZDA_INF_PREAMBLE_BYTES + 3U + MAZDA_INF_PAYLOAD_BYTES + 1U)

#define MAZDA_INF_WINDOW_BYTES 12U
#define MAZDA_INF_WINDOW_CELLS (MAZDA_INF_WINDOW_BYTES * 8U * 2U)

#define MAZDA_INF_UPLOAD_CAPACITY \
    ((MAZDA_INF_WIRE_BYTES * 8U * 2U) + 8U)

#define MAZDA_INF_DATA_FIELD "Data"

static const SubGhzBlockConst subghz_protocol_mazda_infinity_const = {
    .te_short = MAZDA_INF_TE_SHORT,
    .te_long = MAZDA_INF_TE_SHORT * 2U,
    .te_delta = MAZDA_INF_TE_DELTA,
    .min_count_bit_for_found = MAZDA_INF_PAYLOAD_BITS,
};

typedef enum {
    MazdaInfDecoderStepReset = 0,
    MazdaInfDecoderStepData,
} MazdaInfDecoderStep;

struct SubGhzProtocolDecoderMazdaInfinity {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t cells[MAZDA_INF_WINDOW_CELLS];
    uint16_t cell_count;

    uint8_t logical[MAZDA_INF_PAYLOAD_BYTES];
    uint8_t last_logical[MAZDA_INF_PAYLOAD_BYTES];
    bool last_valid;
    bool checksum_ok;
};

struct SubGhzProtocolEncoderMazdaInfinity {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t logical[MAZDA_INF_PAYLOAD_BYTES];
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderMazdaInfinity);

const SubGhzProtocolDecoder subghz_protocol_mazda_infinity_decoder = {
    .alloc = subghz_protocol_decoder_mazda_infinity_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_mazda_infinity_feed,
    .reset = subghz_protocol_decoder_mazda_infinity_reset,
    .get_hash_data = subghz_protocol_decoder_mazda_infinity_get_hash_data,
    .serialize = subghz_protocol_decoder_mazda_infinity_serialize,
    .deserialize = subghz_protocol_decoder_mazda_infinity_deserialize,
    .get_string = subghz_protocol_decoder_mazda_infinity_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_mazda_infinity_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_mazda_infinity");

const SubGhzProtocol subghz_protocol_mazda_infinity = {
    .name = MAZDA_INFINITY_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_mazda_infinity_decoder,
    .encoder = &subghz_protocol_mazda_infinity_encoder_ext.vtable,
};

static uint8_t mazda_inf_parity(uint8_t v) {
    v ^= (uint8_t)(v >> 4);
    v ^= (uint8_t)(v >> 2);
    v ^= (uint8_t)(v >> 1);
    return (uint8_t)(v & 1U);
}

static uint8_t mazda_inf_checksum(const uint8_t b[MAZDA_INF_PAYLOAD_BYTES]) {
    uint32_t sum = 0U;
    for(size_t i = 0U; i < 7U; i++) {
        sum += b[i];
    }
    return (uint8_t)(sum & 0xFFU);
}

static void mazda_inf_interleave(uint8_t b[MAZDA_INF_PAYLOAD_BYTES]) {
    const uint8_t b5 = b[5];
    const uint8_t b6 = b[6];
    b[5] = (uint8_t)((b5 & 0xAAU) | (b6 & 0x55U));
    b[6] = (uint8_t)((b5 & 0x55U) | (b6 & 0xAAU));
}

static void mazda_inf_whiten(uint8_t b[MAZDA_INF_PAYLOAD_BYTES]) {
    const uint8_t p = mazda_inf_parity(b[7]);
    if(p == 1U) {
        for(size_t k = 0U; k < 6U; k++) {
            b[k] ^= b[6];
        }
    } else {
        b[0] ^= b[5];
        b[1] ^= b[5];
        b[2] ^= b[5];
        b[3] ^= b[5];
        b[4] ^= b[5];
        b[6] ^= b[5];
    }
}

static void mazda_inf_wire_to_logical(
    const uint8_t wire[MAZDA_INF_PAYLOAD_BYTES],
    uint8_t logical[MAZDA_INF_PAYLOAD_BYTES]) {
    uint8_t b[MAZDA_INF_PAYLOAD_BYTES];
    for(size_t i = 0U; i < MAZDA_INF_PAYLOAD_BYTES; i++) {
        b[i] = (uint8_t)(255U - wire[i]);
    }

    mazda_inf_whiten(b);
    mazda_inf_interleave(b);
    memcpy(logical, b, MAZDA_INF_PAYLOAD_BYTES);
}

static uint32_t mazda_inf_uid(const uint8_t logical[MAZDA_INF_PAYLOAD_BYTES]) {
    return ((uint32_t)logical[0] << 24U) | ((uint32_t)logical[1] << 16U) |
           ((uint32_t)logical[2] << 8U) | logical[3];
}

static uint16_t mazda_inf_counter(const uint8_t logical[MAZDA_INF_PAYLOAD_BYTES]) {
    return (uint16_t)(((uint16_t)logical[5] << 8U) | logical[6]);
}

static void mazda_inf_decode_fields(SubGhzBlockGeneric* generic, const uint8_t logical[MAZDA_INF_PAYLOAD_BYTES]) {
    generic->serial = mazda_inf_uid(logical);
    generic->btn = logical[4];
    generic->cnt = mazda_inf_counter(logical);
    generic->data_count_bit = MAZDA_INF_PAYLOAD_BITS;

    uint64_t data = 0U;
    for(size_t i = 0U; i < MAZDA_INF_PAYLOAD_BYTES; i++) {
        data = (data << 8U) | logical[i];
    }
    generic->data = data;
}

static bool mazda_inf_frame_valid(const uint8_t logical[MAZDA_INF_PAYLOAD_BYTES]) {
    const uint32_t uid = mazda_inf_uid(logical);
    return uid != 0U && uid != UINT32_MAX;
}

static bool mazda_inf_duration_is_short(uint32_t duration) {
    return DURATION_DIFF(duration, MAZDA_INF_TE_SHORT) < MAZDA_INF_TE_DELTA;
}

static bool mazda_inf_duration_is_long(uint32_t duration) {
    return DURATION_DIFF(duration, MAZDA_INF_TE_SHORT * 2U) < MAZDA_INF_TE_DELTA;
}

static void mazda_inf_clear_cells(SubGhzProtocolDecoderMazdaInfinity* instance) {
    instance->cell_count = 0U;
    memset(instance->cells, 0, sizeof(instance->cells));
}

static bool mazda_inf_cells_to_bytes(
    const uint8_t* cells,
    uint16_t cell_off,
    uint8_t* out,
    size_t bytes) {
    for(size_t bit = 0U; bit < bytes * 8U; bit++) {
        const uint8_t first = cells[cell_off + bit * 2U];
        const uint8_t second = cells[cell_off + bit * 2U + 1U];
        if(first == second) return false;

        const bool value = (first != 0U) && (second == 0U);
        if(bit % 8U == 0U) out[bit / 8U] = 0U;
        if(value) {
            out[bit / 8U] |= (uint8_t)(1U << (7U - (bit & 7U)));
        }
    }
    return true;
}

static void mazda_inf_try_decode(SubGhzProtocolDecoderMazdaInfinity* instance) {

    const uint16_t need_cells = MAZDA_INF_WINDOW_BYTES * 8U * 2U;
    if(instance->cell_count < need_cells) return;

    const uint16_t max_off = (uint16_t)(instance->cell_count - need_cells);
    for(uint16_t off = 0U; off <= max_off; off += 2U) {
        uint8_t win[MAZDA_INF_WINDOW_BYTES];
        if(!mazda_inf_cells_to_bytes(instance->cells, off, win, MAZDA_INF_WINDOW_BYTES)) {
            continue;
        }
        if(win[0] != MAZDA_INF_SYNC0 || win[1] != MAZDA_INF_SYNC1 || win[2] != MAZDA_INF_SYNC2) {
            continue;
        }
        if(win[MAZDA_INF_WINDOW_BYTES - 1U] != MAZDA_INF_TRAILER) {
            continue;
        }

        const uint8_t* wire = &win[3];
        uint8_t logical[MAZDA_INF_PAYLOAD_BYTES];
        mazda_inf_wire_to_logical(wire, logical);

        if(!mazda_inf_frame_valid(logical)) continue;

        const bool cs_ok = (logical[7] == mazda_inf_checksum(logical));

        if(instance->last_valid &&
           memcmp(instance->last_logical, logical, MAZDA_INF_PAYLOAD_BYTES) == 0) {
            return;
        }

        memcpy(instance->logical, logical, MAZDA_INF_PAYLOAD_BYTES);
        memcpy(instance->last_logical, logical, MAZDA_INF_PAYLOAD_BYTES);
        instance->last_valid = true;
        instance->checksum_ok = cs_ok;
        mazda_inf_decode_fields(&instance->generic, logical);

        if(instance->base.callback) {
            instance->base.callback(&instance->base, instance->base.context);
        }
        return;
    }
}

static void mazda_inf_push_cell(SubGhzProtocolDecoderMazdaInfinity* instance, bool level) {
    if(instance->cell_count < MAZDA_INF_WINDOW_CELLS) {
        instance->cells[instance->cell_count++] = level ? 1U : 0U;
    } else {
        memmove(instance->cells, &instance->cells[1], MAZDA_INF_WINDOW_CELLS - 1U);
        instance->cells[MAZDA_INF_WINDOW_CELLS - 1U] = level ? 1U : 0U;
    }
    mazda_inf_try_decode(instance);
}

void* subghz_protocol_decoder_mazda_infinity_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderMazdaInfinity* instance =
        calloc(1, sizeof(SubGhzProtocolDecoderMazdaInfinity));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_mazda_infinity;
    instance->generic.protocol_name = instance->base.protocol->name;
    subghz_protocol_decoder_mazda_infinity_reset(instance);
    return instance;
}

void subghz_protocol_decoder_mazda_infinity_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaInfinity* instance = context;
    instance->decoder.parser_step = MazdaInfDecoderStepReset;
    instance->decoder.decode_data = 0U;
    instance->decoder.decode_count_bit = 0U;
    instance->last_valid = false;
    instance->checksum_ok = false;
    memset(instance->logical, 0, sizeof(instance->logical));
    memset(instance->last_logical, 0, sizeof(instance->last_logical));
    mazda_inf_clear_cells(instance);
}

void subghz_protocol_decoder_mazda_infinity_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaInfinity* instance = context;

    switch(instance->decoder.parser_step) {
    case MazdaInfDecoderStepReset:
        if(mazda_inf_duration_is_short(duration)) {
            mazda_inf_clear_cells(instance);
            instance->decoder.parser_step = MazdaInfDecoderStepData;
            mazda_inf_push_cell(instance, level);
        } else if(mazda_inf_duration_is_long(duration)) {
            mazda_inf_clear_cells(instance);
            instance->decoder.parser_step = MazdaInfDecoderStepData;
            mazda_inf_push_cell(instance, level);
            mazda_inf_push_cell(instance, level);
        }
        break;

    case MazdaInfDecoderStepData:
        if(mazda_inf_duration_is_short(duration)) {
            mazda_inf_push_cell(instance, level);
        } else if(mazda_inf_duration_is_long(duration)) {
            mazda_inf_push_cell(instance, level);
            mazda_inf_push_cell(instance, level);
        } else {

            mazda_inf_try_decode(instance);
            mazda_inf_clear_cells(instance);
            instance->decoder.parser_step = MazdaInfDecoderStepReset;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_mazda_infinity_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaInfinity* instance = context;
    uint8_t hash = 0U;
    for(size_t i = 0U; i < MAZDA_INF_PAYLOAD_BYTES; i++) {
        hash ^= instance->logical[i];
    }
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_mazda_infinity_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaInfinity* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }

    flipper_format_rewind(flipper_format);
    if(!flipper_format_insert_or_update_hex(
           flipper_format, MAZDA_INF_DATA_FIELD, instance->logical, MAZDA_INF_PAYLOAD_BYTES)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    if(!flipper_format_insert_or_update_uint32(
           flipper_format, "Serial", &instance->generic.serial, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    uint32_t btn = instance->generic.btn;
    if(!flipper_format_insert_or_update_uint32(flipper_format, "Btn", &btn, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    uint32_t cnt = instance->generic.cnt;
    if(!flipper_format_insert_or_update_uint32(flipper_format, "Cnt", &cnt, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus subghz_protocol_decoder_mazda_infinity_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaInfinity* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_mazda_infinity_const.min_count_bit_for_found);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }

    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(
           flipper_format, MAZDA_INF_DATA_FIELD, instance->logical, MAZDA_INF_PAYLOAD_BYTES)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    if(!mazda_inf_frame_valid(instance->logical)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    instance->checksum_ok = (instance->logical[7] == mazda_inf_checksum(instance->logical));
    mazda_inf_decode_fields(&instance->generic, instance->logical);
    return SubGhzProtocolStatusOk;
}

void subghz_protocol_decoder_mazda_infinity_get_string(void* context, FuriString* output) {
    SubGhzProtocolDecoderMazdaInfinity* instance = context;
    const SubGhzDecodeInput input = {
        .decoder = context,
        .generic = &instance->generic,
        .extra = NULL,
        .extra_count = 0,
        .aux = NULL,
    };
    subghz_decode_plugin_get_string(instance->generic.protocol_name, &input, output);
}

