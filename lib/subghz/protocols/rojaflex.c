#include "rojaflex.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"
#include <lib/subghz/blocks/bitbuf.h>

#define TAG "RojaFlex"

#define ROJAFLEX_FRAME_BITS_CRC 88
#define ROJAFLEX_CRC_BITS       16
#define ROJAFLEX_MIN_DATA_BITS  (ROJAFLEX_FRAME_BITS_CRC - ROJAFLEX_CRC_BITS)
#define ROJAFLEX_ARR_SIZE       256
#define ROJAFLEX_OVERFLOW       250
#define ROJAFLEX_MSG_BYTES      11
#define ROJAFLEX_CARRIER_OFF_US (32U * 100U)

static const SubGhzBlockConst subghz_protocol_rojaflex_const = {
    .te_short = 100,
    .te_long = 100,
    .te_delta = 25,
    .min_count_bit_for_found = ROJAFLEX_MIN_DATA_BITS,
};

static const uint8_t rojaflex_preamble[48] = {
    1, 0, 1, 0, 1, 0, 1, 0,
    1, 0, 1, 0, 1, 0, 1, 0,
    1, 1, 0, 1, 0, 0, 1, 1,
    1, 0, 0, 1, 0, 0, 0, 1,
    1, 1, 0, 1, 0, 0, 1, 1,
    1, 0, 0, 1, 0, 0, 0, 1,
};

struct SubGhzProtocolDecoderRojaflex {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t all_bits[SUBGHZ_BITBUF_BYTES(ROJAFLEX_ARR_SIZE)];
    uint16_t all_bit_count;
    bool collecting;

    uint8_t msg[ROJAFLEX_MSG_BYTES];
    uint16_t frame_bit_len;
    bool crc_ok;
    uint8_t model;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderRojaflex);

static uint16_t rojaflex_crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for(size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for(int b = 0; b < 8; b++) {
            if(crc & 0x8000)
                crc = (uint16_t)((crc << 1) ^ 0x8005);
            else
                crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static void rojaflex_append_n(SubGhzProtocolDecoderRojaflex* instance, uint32_t n, bool level) {
    uint8_t bit_val = level ? 1 : 0;
    for(uint32_t i = 0; i < n; i++) {
        if(instance->all_bit_count < ROJAFLEX_ARR_SIZE)
            subghz_bitbuf_set(instance->all_bits, instance->all_bit_count++, bit_val);
    }
}

static void rojaflex_process(SubGhzProtocolDecoderRojaflex* instance) {
    const uint8_t* bits = instance->all_bits;
    int n = (int)instance->all_bit_count;
    if(n < 48 + ROJAFLEX_MIN_DATA_BITS) return;

    int sync_pos = -1;
    int search_end = n - 48 - ROJAFLEX_MIN_DATA_BITS;
    for(int i = 0; i <= search_end; i++) {
        bool ok = true;
        for(int j = 0; j < 48; j++) {
            if(subghz_bitbuf_get(bits, i + j) != rojaflex_preamble[j]) {
                ok = false;
                break;
            }
        }
        if(ok) {
            sync_pos = i;
            break;
        }
    }
    if(sync_pos < 0) return;

    int data_start = sync_pos + 48;
    int avail = n - data_start;

    int count = avail & 0xFE;
    if(count < ROJAFLEX_MIN_DATA_BITS || count > ROJAFLEX_FRAME_BITS_CRC) return;

    uint8_t msg[ROJAFLEX_MSG_BYTES];
    memset(msg, 0, sizeof(msg));
    int nbytes = count / 8;
    for(int i = 0; i < count; i++) {
        if(subghz_bitbuf_get(bits, data_start + i)) msg[i >> 3] |= (uint8_t)(0x80 >> (i & 7));
    }

    bool has_crc = (count == ROJAFLEX_FRAME_BITS_CRC);
    bool checked = false;
    if(has_crc) {
        uint16_t crc_msg = (uint16_t)(((uint16_t)msg[9] << 8) | msg[10]);
        if(crc_msg != rojaflex_crc16(msg, 9)) return;
        checked = true;
    } else {

        uint8_t sum = 0;
        for(int i = 1; i < 8; i++) sum = (uint8_t)(sum + msg[i]);
        if(msg[8] != sum) return;
        checked = true;
    }

    uint32_t id = ((uint32_t)msg[1] << 20) | ((uint32_t)msg[2] << 12) | ((uint32_t)msg[3] << 4) |
                  ((uint32_t)msg[4] >> 4);

    uint8_t model;
    switch(msg[5] & 0x0F) {
    case 0x5:
        model = 1;
        break;
    case 0xA:
        model = has_crc ? 0 : 2;
        break;
    default:
        model = 3;
        break;
    }

    uint64_t gdata = 0;
    int top = count < 64 ? count : 64;
    for(int i = 0; i < top; i++)
        gdata = (gdata << 1) | (subghz_bitbuf_get(bits, data_start + i) & 1);

    memset(instance->msg, 0, sizeof(instance->msg));
    for(int i = 0; i < nbytes; i++) instance->msg[i] = msg[i];
    instance->frame_bit_len = (uint16_t)count;
    instance->crc_ok = checked;
    instance->model = model;

    instance->generic.data = gdata;
    instance->generic.data_count_bit = (uint8_t)count;
    instance->generic.serial = id;
    instance->generic.btn = msg[5];
    instance->generic.cnt = 0;
    instance->decoder.decode_data = gdata;
    instance->decoder.decode_count_bit = (uint8_t)count;

    if(instance->base.callback) instance->base.callback(&instance->base, instance->base.context);
}

void* subghz_protocol_decoder_rojaflex_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderRojaflex* instance = calloc(1, sizeof(SubGhzProtocolDecoderRojaflex));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_rojaflex;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_rojaflex_free(void* context) {
    furi_check(context);
    free(context);
}

void subghz_protocol_decoder_rojaflex_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderRojaflex* instance = context;
    instance->all_bit_count = 0;
    instance->collecting = false;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_rojaflex_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderRojaflex* instance = context;
    const uint32_t ts = subghz_protocol_rojaflex_const.te_short;

    if(!level && duration >= ROJAFLEX_CARRIER_OFF_US) {
        if(instance->collecting) rojaflex_process(instance);
        instance->all_bit_count = 0;
        instance->collecting = false;
        return;
    }

    uint32_t n = (duration + ts / 2) / ts;
    if(n == 0) {

        if(instance->collecting) rojaflex_process(instance);
        instance->all_bit_count = 0;
        instance->collecting = false;
        return;
    }
    if(n > 64) n = 64;

    if(!instance->collecting) {
        instance->all_bit_count = 0;
        instance->collecting = true;
    }
    rojaflex_append_n(instance, n, level);

    if(instance->all_bit_count >= ROJAFLEX_OVERFLOW) {
        rojaflex_process(instance);
        instance->all_bit_count = 0;
        instance->collecting = false;
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_rojaflex_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderRojaflex* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t bitlen = instance->frame_bit_len;
    uint32_t crc_ok = instance->crc_ok ? 1 : 0;
    uint32_t model = instance->model;
    if(!flipper_format_write_uint32(flipper_format, "FrameBits", &bitlen, 1) ||
       !flipper_format_write_uint32(flipper_format, "CrcOk", &crc_ok, 1) ||
       !flipper_format_write_uint32(flipper_format, "Model", &model, 1) ||
       !flipper_format_write_hex(flipper_format, "Frame", instance->msg, ROJAFLEX_MSG_BYTES)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_rojaflex_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderRojaflex* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_rojaflex_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;

    uint32_t bitlen = instance->generic.data_count_bit;
    uint32_t crc_ok = 0;
    uint32_t model = 0;
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "FrameBits", &bitlen, 1);
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "CrcOk", &crc_ok, 1);
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Model", &model, 1);
    flipper_format_rewind(flipper_format);
    flipper_format_read_hex(flipper_format, "Frame", instance->msg, ROJAFLEX_MSG_BYTES);

    instance->frame_bit_len = (uint16_t)bitlen;
    instance->crc_ok = crc_ok ? true : false;
    instance->model = (uint8_t)model;
    instance->decoder.decode_data = instance->generic.data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

void subghz_protocol_decoder_rojaflex_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderRojaflex* instance = context;
    uint8_t channel = instance->msg[4] & 0x0F;
    const uint64_t extra[3] = {channel, instance->model, instance->crc_ok ? 1u : 0u};
    const SubGhzDecodeInput input = {
        .generic = &instance->generic,
        .extra = extra,
        .extra_count = 3,
    };
    subghz_decode_plugin_get_string("RojaFlex", &input, output);
}

const SubGhzProtocolDecoder subghz_protocol_rojaflex_decoder = {
    .alloc = subghz_protocol_decoder_rojaflex_alloc,
    .free = subghz_protocol_decoder_rojaflex_free,
    .feed = subghz_protocol_decoder_rojaflex_feed,
    .reset = subghz_protocol_decoder_rojaflex_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_rojaflex_serialize,
    .deserialize = subghz_protocol_decoder_rojaflex_deserialize,
    .get_string = subghz_protocol_decoder_rojaflex_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_rojaflex_encoder_ext =
    SUBGHZ_ENCODER_EXTERNAL("enc_rojaflex");

const SubGhzProtocol subghz_protocol_rojaflex = {
    .name = SUBGHZ_PROTOCOL_ROJAFLEX_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_rojaflex_decoder,
    .encoder = &subghz_protocol_rojaflex_encoder_ext.vtable,
};
