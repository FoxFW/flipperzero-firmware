#include "honda_v10.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include <lib/subghz/blocks/bitbuf.h>

#define TAG "HondaV10"

// Honda V10 ("V2H4") -- ported from firmware honda_v10.c via KeeLoq-RX arke_honda.py.
// Native 2-FSK Manchester, te 104/208 us, te_delta 35. Half-bit samples collected into
// hb[]; a sliding dual-polarity Manchester decode across offsets yields a 96-bit
// (12-byte) MSB-first frame. Fields:
//   serial=dec[0..2] (24b), button=dec[3]>>4, flags=dec[3]&0xF,
//   counter = 0x00FFFFFF ^ dec[4..6] (inverted 24b), status=dec[7], tail=dec[8..11] (32b).
// NO data CRC: emission gated on flags==0x7 + button in {0xF,0xE} + valid serial.
// The 32-bit tail is a per-counter encrypted MAC (replay-only; not a clone). crc_ok=false
// is reported in get_string only to mean "no CRC present". Decode-only; .encoder = NULL.

#define HV10_TS          104
#define HV10_TL          208
#define HV10_TD          35
#define HV10_MIN_PRE     20
#define HV10_FRAME_BITS  96
#define HV10_MIN_BITS    88
#define HV10_FRAME_BYTES 12
#define HV10_HB_CAP      2048

static const SubGhzBlockConst subghz_protocol_honda_v10_const = {
    .te_short = HV10_TS,
    .te_long = HV10_TL,
    .te_delta = HV10_TD,
    .min_count_bit_for_found = HV10_FRAME_BITS,
};

struct SubGhzProtocolDecoderHondaV10 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t hb[SUBGHZ_BITBUF_BYTES(HV10_HB_CAP)];
    uint16_t hb_len;

    uint32_t serial;
    uint8_t btn;
    uint8_t flags;
    uint32_t cnt;
    uint8_t status;
    uint32_t tail;
    bool pol_inv;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderHondaV10);


static uint8_t honda_v10_classify(uint32_t dur) {
    if(DURATION_DIFF(dur, HV10_TS) <= HV10_TD) return 1;
    if(DURATION_DIFF(dur, HV10_TL) <= HV10_TD) return 2;
    return 0;
}

// Mirror HondaV10._man_decode(). Returns bit_count (0 on fail) and *consumed.
static int honda_v10_man_decode(
    const uint8_t* hb,
    int n,
    int start_ofs,
    bool invert,
    uint8_t* decoded,
    int* consumed) {
    memset(decoded, 0, HV10_FRAME_BYTES + 1);
    if(start_ofs + 1 >= n) {
        *consumed = start_ofs;
        return 0;
    }
    int alt_start = start_ofs;
    while(alt_start + 1 < n) {
        int alt_end = alt_start + 1;
        while(alt_end < n && subghz_bitbuf_get(hb, alt_end) != subghz_bitbuf_get(hb, alt_end - 1)) alt_end++;
        int run_len = alt_end - alt_start;
        if(run_len >= HV10_MIN_PRE && alt_end < n) {
            int i = alt_end;
            while(i + 1 < n && subghz_bitbuf_get(hb, i) == subghz_bitbuf_get(hb, i + 1)) i++;
            int bc = 0;
            memset(decoded, 0, HV10_FRAME_BYTES + 1);
            while(i + 1 < n && bc < HV10_FRAME_BITS) {
                uint8_t h0 = subghz_bitbuf_get(hb, i), h1 = subghz_bitbuf_get(hb, i + 1);
                if(h0 == h1) break;
                int bv =
                    !invert ? ((h0 == 0 && h1 == 1) ? 1 : 0) : ((h0 == 1 && h1 == 0) ? 1 : 0);
                if(bv) decoded[bc >> 3] |= (uint8_t)(1u << (7 - (bc & 7)));
                bc++;
                i += 2;
            }
            *consumed = i;
            if(bc >= HV10_MIN_BITS) return bc;
            return 0;
        }
        alt_start = alt_end;
    }
    *consumed = n;
    return 0;
}

// Mirror HondaV10._try_polarity_at(). *next set like the Python second return value.
static bool honda_v10_try_polarity_at(
    SubGhzProtocolDecoderHondaV10* instance,
    bool invert,
    int start_ofs,
    int* next) {
    uint8_t decoded[HV10_FRAME_BYTES + 1];
    int consumed = 0;
    int bit_count =
        honda_v10_man_decode(instance->hb, instance->hb_len, start_ofs, invert, decoded, &consumed);
    if(bit_count == 0) {
        *next = instance->hb_len;
        return false;
    }
    *next = consumed;
    if(bit_count < HV10_MIN_BITS) return false;

    uint32_t serial = ((uint32_t)decoded[0] << 16) | ((uint32_t)decoded[1] << 8) | decoded[2];
    uint8_t button = (decoded[3] >> 4) & 0x0F;
    uint8_t flags = decoded[3] & 0x0F;
    uint32_t counter =
        0x00FFFFFFU ^ (((uint32_t)decoded[4] << 16) | ((uint32_t)decoded[5] << 8) | decoded[6]);
    uint8_t status = decoded[7];
    uint32_t tail = ((uint32_t)decoded[8] << 24) | ((uint32_t)decoded[9] << 16) |
                    ((uint32_t)decoded[10] << 8) | decoded[11];

    if(button != 0xF && button != 0xE) return false;
    if(serial == 0 || serial == 0x00FFFFFF) return false;
    if(flags != 0x7) return false;

    uint64_t data = 0;
    for(int i = 0; i < 8; i++) data = (data << 8) | decoded[i];

    instance->serial = serial;
    instance->btn = button;
    instance->flags = flags;
    instance->cnt = counter;
    instance->status = status;
    instance->tail = tail;
    instance->pol_inv = invert;

    instance->generic.data = data;
    instance->generic.data_count_bit = HV10_FRAME_BITS;
    instance->generic.serial = serial;
    instance->generic.btn = button;
    instance->generic.cnt = counter;
    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = HV10_FRAME_BITS;

    if(instance->base.callback) instance->base.callback(&instance->base, instance->base.context);
    return true;
}

// Mirror HondaV10._try_decode(): sliding scan over offsets, dual polarity.
static void honda_v10_try_decode(SubGhzProtocolDecoderHondaV10* instance) {
    int n = instance->hb_len;
    if(n < (HV10_MIN_PRE + 16)) return;
    int ofs = 0;
    while(ofs + HV10_MIN_PRE + 16 <= n) {
        int nxt_nor = n, nxt_inv = n;
        bool ok_nor = honda_v10_try_polarity_at(instance, false, ofs, &nxt_nor);
        bool ok_inv = false;
        if(!ok_nor) ok_inv = honda_v10_try_polarity_at(instance, true, ofs, &nxt_inv);
        if(ok_nor || ok_inv) {
            int nxt = ok_nor ? nxt_nor : nxt_inv;
            ofs = (nxt > ofs) ? nxt : ofs + 1;
            continue;
        }
        int skip = ofs + 1;
        while(skip < n && subghz_bitbuf_get(instance->hb, skip) != subghz_bitbuf_get(instance->hb, skip - 1)) skip++;
        ofs = (skip > ofs) ? skip : ofs + 1;
    }
}

void* subghz_protocol_decoder_honda_v10_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderHondaV10* instance = calloc(1, sizeof(SubGhzProtocolDecoderHondaV10));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_honda_v10;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}



void subghz_protocol_decoder_honda_v10_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV10* instance = context;
    instance->hb_len = 0;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_honda_v10_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV10* instance = context;
    uint8_t lvl = level ? 1 : 0;
    uint8_t dc = honda_v10_classify(duration);
    if(dc == 1) {
        if(instance->hb_len < HV10_HB_CAP) subghz_bitbuf_set(instance->hb, instance->hb_len++, lvl);
    } else if(dc == 2) {
        if(instance->hb_len + 2 <= HV10_HB_CAP) {
            subghz_bitbuf_set(instance->hb, instance->hb_len++, lvl);
            subghz_bitbuf_set(instance->hb, instance->hb_len++, lvl);
        }
    } else {
        if(instance->hb_len >= (HV10_MIN_PRE + 16)) honda_v10_try_decode(instance);
        instance->hb_len = 0;
    }
}



SubGhzProtocolStatus subghz_protocol_decoder_honda_v10_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV10* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t serial = instance->serial;
    uint32_t btn = instance->btn;
    uint32_t cnt = instance->cnt;
    uint32_t flags = instance->flags;
    uint32_t status = instance->status;
    uint32_t tail = instance->tail;
    if(!flipper_format_write_uint32(flipper_format, "Serial", &serial, 1) ||
       !flipper_format_write_uint32(flipper_format, "Btn", &btn, 1) ||
       !flipper_format_write_uint32(flipper_format, "Cnt", &cnt, 1) ||
       !flipper_format_write_uint32(flipper_format, "Flags", &flags, 1) ||
       !flipper_format_write_uint32(flipper_format, "Status", &status, 1) ||
       !flipper_format_write_uint32(flipper_format, "Tail", &tail, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_honda_v10_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV10* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_honda_v10_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;

    uint64_t data = instance->generic.data;
    uint8_t dec[8];
    for(int i = 0; i < 8; i++) dec[i] = (uint8_t)(data >> (8 * (7 - i)));
    instance->serial = ((uint32_t)dec[0] << 16) | ((uint32_t)dec[1] << 8) | dec[2];
    instance->btn = (dec[3] >> 4) & 0x0F;
    instance->flags = dec[3] & 0x0F;
    instance->cnt =
        0x00FFFFFFU ^ (((uint32_t)dec[4] << 16) | ((uint32_t)dec[5] << 8) | dec[6]);
    instance->status = dec[7];

    uint32_t tmp = 0;
    flipper_format_rewind(flipper_format);
    instance->tail = flipper_format_read_uint32(flipper_format, "Tail", &tmp, 1) ? tmp : 0;

    instance->generic.serial = instance->serial;
    instance->generic.btn = instance->btn;
    instance->generic.cnt = instance->cnt;
    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

const SubGhzProtocolDecoder subghz_protocol_honda_v10_decoder = {
    .alloc = subghz_protocol_decoder_honda_v10_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_honda_v10_feed,
    .reset = subghz_protocol_decoder_honda_v10_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_honda_v10_serialize,
    .deserialize = subghz_protocol_decoder_honda_v10_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

const SubGhzProtocol subghz_protocol_honda_v10 = {
    .name = HONDA_V10_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_honda_v10_decoder,
    .encoder = NULL,
};
