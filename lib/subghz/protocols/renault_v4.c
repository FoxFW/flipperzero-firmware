#include "renault_v4.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"

#define TAG "RenaultV4"

// Renault/xHorse V4 -- ported from firmware renault_v4.c via KeeLoq-RX arke_renault.py.
// Decodes the Nissan/xHorse aftermarket UNIVERSAL keyfob: 88-bit Manchester, te 125/250.
// Recovers Serial(32) + Button nibble + a VISIBLE (cleartext) 8-bit counter (+1 per press).
// Strong self-check (all must hold): XOR of all 11 payload bytes == 0xFF, button nibble in
// {0xE,0xD,0xB,0x7}, and 2-bit pad field == 0b01. Genuine encrypted Renault (v1/v2/v3) only
// exposes a pseudo-serial or nothing; this V4 is the one with a recoverable serial+counter.
// Decode-only (rolling next-code; not a replayable clone); .encoder = NULL.

#define RENAULT_V4_SYNC_MIN   800
#define RENAULT_V4_DATA_BITS  88
#define RENAULT_V4_ARR_SIZE   128

static const SubGhzBlockConst subghz_protocol_renault_v4_const = {
    .te_short = 125,
    .te_long = 250,
    .te_delta = 70,
    .min_count_bit_for_found = RENAULT_V4_DATA_BITS,
};

struct SubGhzProtocolDecoderRenaultV4 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    bool synced;
    uint8_t man_state;
    uint8_t bits[RENAULT_V4_ARR_SIZE];
    uint16_t bit_count;

    uint8_t last_key[11];
    bool last_valid;

    uint32_t serial;
    uint8_t btn;
    uint8_t cnt; // cleartext 8-bit counter
    uint32_t data_tail; // low 24 bits of the 88-bit frame (bits 64..87)
    bool crc_ok;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderRenaultV4);


// Custom Manchester state machine (firmware _rv4_manchester_decode):
// returns true if a bit was produced; r1 encodes (width,level): 0=short-high,
// 1=short-low, 2=long-high, 3=long-low. Transcribed 1:1 from arke_renault.py.
static bool rv4_man_decode(uint8_t* man, uint8_t r1, uint8_t* bit) {
    uint8_t s = *man;
    if(r1 == 0) { // short high
        if(s == 0 || s == 2) {
            *man = 1;
            return false;
        }
        if(s == 1) {
            *man = 0;
            *bit = 0;
            return true;
        }
        if(s == 3) {
            *man = 2;
            *bit = 0;
            return true;
        }
    } else if(r1 == 1) { // short low
        if(s == 0 || s == 2) {
            *man = 3;
            return false;
        }
        if(s == 1) {
            *man = 2;
            *bit = 1;
            return true;
        }
        if(s == 3) {
            *man = 0;
            *bit = 1;
            return true;
        }
    } else if(r1 == 2) { // long high
        if(s == 0 || s == 2) {
            *man = 0;
            *bit = 0;
            return true;
        }
        if(s == 1 || s == 3) {
            *man = 1;
            return false;
        }
    } else if(r1 == 3) { // long low
        if(s == 0 || s == 2) {
            *man = 2;
            *bit = 1;
            return true;
        }
        if(s == 1 || s == 3) {
            *man = 3;
            return false;
        }
    }
    return false;
}

// Mirror of arke_renault.RenaultV4._process(): scan the accumulated decoded bit
// buffer for a >=10-bit preamble (both polarities), gate the frame length, build
// the 11-byte frame, run the XOR==0xFF + button-whitelist + pad self-check, then
// recover serial + button + cleartext counter. Emits via callback on success.
static void renault_v4_process(SubGhzProtocolDecoderRenaultV4* instance) {
    const uint8_t* bits = instance->bits;
    int n = (int)instance->bit_count;

    for(int inv = 0; inv < 2; inv++) {
        uint8_t pv = inv ? 0 : 1;
        int pre = 0;
        while(pre < n && bits[pre] == pv) pre++;
        if(pre < 10) continue;

        int ds = pre + 1;
        int dlen = n - ds;
        if(dlen < RENAULT_V4_DATA_BITS || dlen > RENAULT_V4_DATA_BITS + 4) continue;

        int use = dlen < RENAULT_V4_DATA_BITS ? dlen : RENAULT_V4_DATA_BITS;
        uint8_t frame[12] = {0};
        for(int i = 0; i < use; i++) {
            if(bits[ds + i]) frame[i >> 3] |= (uint8_t)(0x80 >> (i & 7));
        }

        uint8_t xr = 0;
        for(int i = 0; i < 11; i++) xr ^= frame[i];
        if(xr != 0xFF) continue;

        uint8_t btn_nib = (uint8_t)((frame[4] >> 4) & 0x0F);
        if(btn_nib != 0xE && btn_nib != 0xD && btn_nib != 0xB && btn_nib != 0x7) continue;

        if(((frame[4] >> 2) & 0x03) != 0x01) continue; // pad invariant

        uint32_t serial = ((uint32_t)frame[0] << 24) | ((uint32_t)frame[1] << 16) |
                          ((uint32_t)frame[2] << 8) | (uint32_t)frame[3];
        uint8_t counter8 = (uint8_t)(((frame[4] & 0x03) << 6) | ((frame[5] >> 2) & 0x3F));

        // de-dup identical frame within one capture (firmware keeps "last").
        if(instance->last_valid) {
            bool same = true;
            for(int i = 0; i < 11; i++) {
                if(instance->last_key[i] != frame[i]) {
                    same = false;
                    break;
                }
            }
            if(same) return;
        }
        for(int i = 0; i < 11; i++) instance->last_key[i] = frame[i];
        instance->last_valid = true;

        uint64_t key = 0; // top 64 bits = frame[0..7]
        for(int i = 0; i < 8; i++) key = (key << 8) | frame[i];
        uint32_t tail = ((uint32_t)frame[8] << 16) | ((uint32_t)frame[9] << 8) | (uint32_t)frame[10];

        instance->serial = serial;
        instance->btn = btn_nib;
        instance->cnt = counter8;
        instance->data_tail = tail;
        instance->crc_ok = true; // XOR self-check passed

        instance->generic.data = key;
        instance->generic.data_count_bit = RENAULT_V4_DATA_BITS;
        instance->generic.serial = serial;
        instance->generic.btn = btn_nib;
        instance->generic.cnt = counter8;
        instance->decoder.decode_data = key;
        instance->decoder.decode_count_bit = RENAULT_V4_DATA_BITS;

        if(instance->base.callback)
            instance->base.callback(&instance->base, instance->base.context);
        return;
    }
}

void* subghz_protocol_decoder_renault_v4_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderRenaultV4* instance = calloc(1, sizeof(SubGhzProtocolDecoderRenaultV4));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_renault_v4;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->man_state = 1;
    return instance;
}



void subghz_protocol_decoder_renault_v4_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultV4* instance = context;
    instance->synced = false;
    instance->man_state = 1;
    instance->bit_count = 0;
    instance->last_valid = false;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_renault_v4_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultV4* instance = context;
    uint32_t ts = subghz_protocol_renault_v4_const.te_short;
    uint32_t tl = subghz_protocol_renault_v4_const.te_long;
    uint32_t d = subghz_protocol_renault_v4_const.te_delta;

    if(!instance->synced) {
        if(level && duration >= RENAULT_V4_SYNC_MIN) {
            instance->synced = true;
            instance->bit_count = 0;
            instance->man_state = 1;
        }
        return;
    }

    bool is_short = DURATION_DIFF(duration, ts) < d;
    bool is_long = DURATION_DIFF(duration, tl) < d;

    if(is_short || is_long) {
        uint8_t r1;
        if(!is_long) {
            r1 = level ? 0 : 1;
        } else {
            r1 = level ? 2 : 3;
        }
        uint8_t bit = 0;
        if(rv4_man_decode(&instance->man_state, r1, &bit)) {
            if(instance->bit_count < RENAULT_V4_ARR_SIZE) {
                instance->bits[instance->bit_count++] = bit;
            }
        }
    } else {
        renault_v4_process(instance);
        if(level && duration >= RENAULT_V4_SYNC_MIN) {
            instance->bit_count = 0;
            instance->man_state = 1;
        } else {
            instance->synced = false;
        }
    }
}



SubGhzProtocolStatus subghz_protocol_decoder_renault_v4_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultV4* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t serial = instance->serial;
    uint32_t btn = instance->btn;
    uint32_t cnt = instance->cnt;
    uint32_t tail = instance->data_tail;
    if(!flipper_format_write_uint32(flipper_format, "Serial", &serial, 1) ||
       !flipper_format_write_uint32(flipper_format, "Btn", &btn, 1) ||
       !flipper_format_write_uint32(flipper_format, "Cnt", &cnt, 1) ||
       !flipper_format_write_uint32(flipper_format, "Data2", &tail, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_renault_v4_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultV4* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_renault_v4_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;

    uint64_t key = instance->generic.data;
    uint8_t frame4 = (uint8_t)((key >> 24) & 0xFFU);
    uint8_t frame5 = (uint8_t)((key >> 16) & 0xFFU);
    instance->serial = (uint32_t)(key >> 32);
    instance->btn = (uint8_t)((frame4 >> 4) & 0x0FU);
    instance->cnt = (uint8_t)(((frame4 & 0x03) << 6) | ((frame5 >> 2) & 0x3F));

    uint32_t tail = 0;
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Data2", &tail, 1)) {
        instance->data_tail = tail & 0x00FFFFFFU;
    } else {
        instance->data_tail = 0;
    }
    instance->crc_ok = true;

    instance->generic.serial = instance->serial;
    instance->generic.btn = instance->btn;
    instance->generic.cnt = instance->cnt;
    instance->decoder.decode_data = key;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

const SubGhzProtocolDecoder subghz_protocol_renault_v4_decoder = {
    .alloc = subghz_protocol_decoder_renault_v4_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_renault_v4_feed,
    .reset = subghz_protocol_decoder_renault_v4_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_renault_v4_serialize,
    .deserialize = subghz_protocol_decoder_renault_v4_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

const SubGhzProtocol subghz_protocol_renault_v4 = {
    .name = RENAULT_V4_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_renault_v4_decoder,
    .encoder = NULL,
};
