#include "honda_v9.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include "../encode_plugin.h"
#include <lib/subghz/blocks/bitbuf.h>

#define TAG "HondaV9"

#define HV9_TS         63
#define HV9_TL         126
#define HV9_TD         25
#define HV9_MIN_PRE    20
#define HV9_PRE_CYC    55
#define HV9_FRAME_BITS 136
#define HV9_MIN_ACCEPT 134
#define HV9_MIN_BITS   64
#define HV9_FRAME_BYTES 17
#define HV9_HB_CAP     512

static const SubGhzBlockConst subghz_protocol_honda_v9_const = {
    .te_short = HV9_TS,
    .te_long = HV9_TL,
    .te_delta = HV9_TD,
    .min_count_bit_for_found = HV9_MIN_ACCEPT,
};

struct SubGhzProtocolDecoderHondaV9 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t hb[SUBGHZ_BITBUF_BYTES(HV9_HB_CAP)];
    uint16_t hb_len;

    uint32_t serial;
    uint8_t btn;
    uint16_t cnt;
    uint8_t hdr;
    uint8_t flags;
    uint32_t fingerprint;
    uint8_t crc;
    bool crc_ok;
    bool pol_inv;
    uint64_t tail;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderHondaV9);

static uint8_t honda_v9_classify(uint32_t dur) {
    if(DURATION_DIFF(dur, HV9_TS) <= HV9_TD) return 1;
    if(DURATION_DIFF(dur, HV9_TL) <= HV9_TD) return 2;
    return 0;
}

static int honda_v9_man_decode(const uint8_t* hb, int n, bool invert, uint8_t* decoded) {
    memset(decoded, 0, HV9_FRAME_BYTES + 2);
    int alt_end = 1;
    while(alt_end < n && subghz_bitbuf_get(hb, alt_end) != subghz_bitbuf_get(hb, alt_end - 1)) alt_end++;
    if(alt_end == n || alt_end < HV9_MIN_PRE) return 0;
    int i;
    if(alt_end >= HV9_PRE_CYC * 2) {
        i = HV9_PRE_CYC * 2;
    } else {
        i = alt_end;
        while(i + 1 < n && subghz_bitbuf_get(hb, i) == subghz_bitbuf_get(hb, i + 1)) i++;
    }
    int bit_count = 0;
    while(i + 1 < n && bit_count < HV9_FRAME_BITS) {
        uint8_t h0 = subghz_bitbuf_get(hb, i), h1 = subghz_bitbuf_get(hb, i + 1);
        if(h0 == h1) break;
        int bit_val;
        if(!invert)
            bit_val = (h0 == 0 && h1 == 1) ? 1 : 0;
        else
            bit_val = (h0 == 1 && h1 == 0) ? 1 : 0;
        if(bit_val) decoded[bit_count >> 3] |= (uint8_t)(1u << (7 - (bit_count & 7)));
        bit_count++;
        i += 2;
    }
    if(bit_count < HV9_MIN_BITS) return 0;
    return bit_count;
}

static uint8_t honda_v9_crc8(const uint8_t* data, int len) {
    uint8_t c = 0x00;
    for(int i = 0; i < len; i++) {
        c ^= data[i];
        for(int b = 0; b < 8; b++)
            c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x2F) : (uint8_t)(c << 1);
    }
    return c ^ 0x0C;
}

static bool honda_v9_try_polarity(SubGhzProtocolDecoderHondaV9* instance, bool invert) {
    uint8_t decoded[HV9_FRAME_BYTES + 2];
    int bit_count = honda_v9_man_decode(instance->hb, instance->hb_len, invert, decoded);
    if(bit_count < HV9_MIN_ACCEPT) return false;

    uint8_t hdr = (decoded[0] >> 4) & 0x0F;
    uint8_t command = decoded[8];
    uint32_t serial =
        ((uint32_t)decoded[1] << 12) | ((uint32_t)decoded[2] << 4) | ((decoded[3] >> 4) & 0x0F);
    uint8_t flags = decoded[3] & 0x0F;
    uint32_t fingerprint = ((uint32_t)decoded[4] << 24) | ((uint32_t)decoded[5] << 16) |
                           ((uint32_t)decoded[6] << 8) | decoded[7];
    uint16_t counter = ((uint16_t)decoded[12] << 8) | decoded[13];

    uint8_t crc = honda_v9_crc8(decoded, 16);
    int crc_bits = bit_count - 128;
    if(crc_bits < 6 || crc_bits > 8) return false;
    uint8_t shift = (uint8_t)(8 - crc_bits);
    if((uint8_t)(crc >> shift) != (uint8_t)(decoded[16] >> shift)) return false;

    if(serial == 0 || serial == 0x000FFFFF) return false;
    if(fingerprint == 0 || fingerprint == 0xFFFFFFFF) return false;

    uint64_t data = 0;
    for(int i = 0; i < 8; i++) data = (data << 8) | decoded[i];
    uint64_t tail = 0;
    for(int i = 8; i < 16; i++) tail = (tail << 8) | decoded[i];

    instance->serial = serial;
    instance->btn = command;
    instance->cnt = counter;
    instance->hdr = hdr;
    instance->flags = flags;
    instance->fingerprint = fingerprint;
    instance->crc = decoded[16];
    instance->crc_ok = true;
    instance->pol_inv = invert;
    instance->tail = tail;

    instance->generic.data = data;
    instance->generic.data_count_bit = (uint16_t)bit_count;
    instance->generic.serial = serial;
    instance->generic.btn = command;
    instance->generic.cnt = counter;
    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = (uint16_t)bit_count;

    if(instance->base.callback) instance->base.callback(&instance->base, instance->base.context);
    return true;
}

static void honda_v9_try_decode(SubGhzProtocolDecoderHondaV9* instance) {
    if(instance->hb_len < (HV9_MIN_PRE + 16)) return;
    if(honda_v9_try_polarity(instance, false)) return;
    honda_v9_try_polarity(instance, true);
}

void* subghz_protocol_decoder_honda_v9_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderHondaV9* instance = calloc(1, sizeof(SubGhzProtocolDecoderHondaV9));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_honda_v9;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_honda_v9_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV9* instance = context;
    instance->hb_len = 0;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_honda_v9_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV9* instance = context;
    uint8_t lvl = level ? 1 : 0;
    uint8_t dc = honda_v9_classify(duration);
    if(dc == 1) {
        if(instance->hb_len < HV9_HB_CAP) subghz_bitbuf_set(instance->hb, instance->hb_len++, lvl);
    } else if(dc == 2) {
        if(instance->hb_len + 2 <= HV9_HB_CAP) {
            subghz_bitbuf_set(instance->hb, instance->hb_len++, lvl);
            subghz_bitbuf_set(instance->hb, instance->hb_len++, lvl);
        }
    } else {
        if(instance->hb_len >= (HV9_MIN_PRE + 16)) honda_v9_try_decode(instance);
        instance->hb_len = 0;
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_honda_v9_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV9* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t serial = instance->serial;
    uint32_t btn = instance->btn;
    uint32_t cnt = instance->cnt;
    uint32_t fp = instance->fingerprint;
    uint32_t crc = instance->crc;
    uint32_t tail_hi = (uint32_t)(instance->tail >> 32);
    uint32_t tail_lo = (uint32_t)(instance->tail & 0xFFFFFFFFU);
    if(!flipper_format_write_uint32(flipper_format, "Serial", &serial, 1) ||
       !flipper_format_write_uint32(flipper_format, "Btn", &btn, 1) ||
       !flipper_format_write_uint32(flipper_format, "Cnt", &cnt, 1) ||
       !flipper_format_write_uint32(flipper_format, "Fingerprint", &fp, 1) ||
       !flipper_format_write_uint32(flipper_format, "CRC", &crc, 1) ||
       !flipper_format_write_uint32(flipper_format, "TailHi", &tail_hi, 1) ||
       !flipper_format_write_uint32(flipper_format, "TailLo", &tail_lo, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    uint8_t fdata[17];
    uint64_t g = instance->generic.data;
    for(uint8_t b = 0; b < 8; b++) fdata[b] = (uint8_t)(g >> (56 - b * 8));
    uint64_t t = instance->tail;
    for(uint8_t b = 0; b < 8; b++) fdata[8 + b] = (uint8_t)(t >> (56 - b * 8));
    fdata[16] = instance->crc;
    if(!flipper_format_write_hex(flipper_format, "Data", fdata, sizeof(fdata))) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_honda_v9_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV9* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_honda_v9_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;

    uint8_t dec[17] = {0};
    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(flipper_format, "Data", dec, 17)) {

        uint64_t d0 = instance->generic.data;
        for(int i = 0; i < 8; i++) dec[i] = (uint8_t)(d0 >> (8 * (7 - i)));
    }
    instance->hdr = (dec[0] >> 4) & 0x0F;
    instance->btn = dec[8];
    instance->serial =
        ((uint32_t)dec[1] << 12) | ((uint32_t)dec[2] << 4) | ((dec[3] >> 4) & 0x0F);
    instance->flags = dec[3] & 0x0F;
    instance->fingerprint = ((uint32_t)dec[4] << 24) | ((uint32_t)dec[5] << 16) |
                            ((uint32_t)dec[6] << 8) | dec[7];
    instance->cnt = ((uint16_t)dec[12] << 8) | dec[13];
    instance->crc = dec[16];

    uint8_t crc = honda_v9_crc8(dec, 16);
    int crc_bits = (int)instance->generic.data_count_bit - 128;
    uint8_t shift = (crc_bits >= 8) ? 0u : (uint8_t)(8 - (crc_bits < 0 ? 0 : crc_bits));
    instance->crc_ok =
        (crc_bits >= 6) && ((uint8_t)(crc >> shift) == (uint8_t)(dec[16] >> shift));

    uint64_t data = 0;
    for(int i = 0; i < 8; i++) data = (data << 8) | dec[i];
    instance->tail = 0;
    for(int i = 8; i < 16; i++) instance->tail = (instance->tail << 8) | dec[i];

    instance->generic.data = data;
    instance->generic.serial = instance->serial;
    instance->generic.btn = instance->btn;
    instance->generic.cnt = instance->cnt;
    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

const SubGhzProtocolDecoder subghz_protocol_honda_v9_decoder = {
    .alloc = subghz_protocol_decoder_honda_v9_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_honda_v9_feed,
    .reset = subghz_protocol_decoder_honda_v9_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_honda_v9_serialize,
    .deserialize = subghz_protocol_decoder_honda_v9_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_honda_v9_encoder_ext =
    SUBGHZ_ENCODER_EXTERNAL("enc_recon_manchester");

const SubGhzProtocol subghz_protocol_honda_v9 = {
    .name = HONDA_V9_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_honda_v9_decoder,
    .encoder = &subghz_protocol_honda_v9_encoder_ext.vtable,
};
