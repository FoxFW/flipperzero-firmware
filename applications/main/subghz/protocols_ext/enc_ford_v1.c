/* enc_ford_v1.fal : SD encoder for ford_v1 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define ford_protocol_v1 enc_ford_v1__ford_protocol_v1
#define subghz_protocol_encoder_ford_v1_alloc enc_ford_v1__subghz_protocol_encoder_ford_v1_alloc
#define subghz_protocol_encoder_ford_v1_deserialize enc_ford_v1__subghz_protocol_encoder_ford_v1_deserialize
#define subghz_protocol_encoder_ford_v1_free enc_ford_v1__subghz_protocol_encoder_ford_v1_free
#define subghz_protocol_encoder_ford_v1_stop enc_ford_v1__subghz_protocol_encoder_ford_v1_stop
#define subghz_protocol_encoder_ford_v1_yield enc_ford_v1__subghz_protocol_encoder_ford_v1_yield
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/ford_v1.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <string.h>
#include <lib/subghz/protocols/common.h>

#define FORD_V1_DATA_BYTES   17
#define ford_v1_crc16(data, len) subghz_protocol_blocks_crc16((data), (len), 0x1021, 0x0000)
#define FORD_V1_ENC_BURST_COUNT    6U
#define FORD_V1_ENC_PREAMBLE_PAIRS 400U
#define FORD_V1_ENC_BURST_LD_COUNT \
    ((FORD_V1_ENC_PREAMBLE_PAIRS * 2U) + 2U + (FORD_V1_DATA_BYTES * 16U) + 1U)
#define FORD_V1_ENC_SYNC_SHORT_US 65U
#define FORD_V1_ENC_SYNC_LONG_US  130U
#define FORD_V1_ENC_GAP_REPEAT_US 50000U
#define FORD_V1_ENC_GAP_LAST_US   260U

typedef struct SubGhzProtocolEncoderFordV1 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    uint64_t data2;
    uint16_t crc_calc;
    uint8_t plain9[9];
    uint8_t raw_tx[FORD_V1_DATA_BYTES];
    uint8_t encryption_supported;
    uint8_t plain_valid;
    uint8_t burst_idx;
} SubGhzProtocolEncoderFordV1;

static const SubGhzBlockConst subghz_protocol_ford_v1_const = {
    .te_short = 65,
    .te_long = 130,
    .te_delta = 39,
    .min_count_bit_for_found = 136,
};

static const uint8_t ford_v1_encoder_burst_pkt4_vals[6] = {0x08, 0x00, 0x10, 0x08, 0x00, 0x10};

static void ford_v1_decode_with_flag(uint8_t* raw, size_t len, uint8_t flag_byte);
static void ford_v1_encode_inverse_block(uint8_t block[9]);
static void ford_v1_encode_air_9bytes(const uint8_t* plain9, uint8_t* air9_out);
static bool ford_v1_plain_from_air(const uint8_t air9[9], uint8_t plain9_out[9]);
static bool ford_v1_extract_plain_from_raw(
    const uint8_t* raw17_in,
    uint8_t* plain9_out,
    uint8_t* raw17_canonical_out_opt);
static uint16_t ford_v1_crc16_from_key3_bytes(const uint8_t key3[4]);
static uint64_t ford_v1_u64_from_be_key8(const uint8_t key8[8]);
static uint64_t ford_v1_u64_legacy_halves_bswap(const uint8_t key8[8]);
static void
    ford_v1_raw14_from_internal_keys(uint64_t key1, uint64_t data2, uint8_t raw14_out[14]);
static bool ford_v1_keys_file_to_canonical_raw(
    const uint8_t key1b[8],
    const uint8_t key2b[8],
    uint16_t crc16,
    uint64_t* key1_out,
    uint64_t* key2_out,
    uint8_t raw17[FORD_V1_DATA_BYTES],
    uint8_t plain9[9]);
static void
    ford_v1_plain_apply_fields(uint8_t* plain9, uint32_t serial, uint8_t btn, uint32_t cnt);
static void ford_v1_encoder_rebuild_raw_from_plain(uint8_t* raw17, const uint8_t* plain9);
static void ford_v1_encoder_keys_from_raw(SubGhzProtocolEncoderFordV1* instance);
static uint32_t ford_v1_encoder_preset_hop_read_stub(void);
static void ford_v1_encoder_apply_counter_cap(uint32_t* cnt_p, uint32_t cap);
static void ford_v1_encoder_patch_key1_low_bits(SubGhzProtocolEncoderFordV1* instance);
static void ford_v1_encoder_build_burst(SubGhzProtocolEncoderFordV1* instance, uint8_t burst_idx);

static void ford_v1_decode_with_flag(uint8_t* raw, size_t len, uint8_t flag_byte) {
    if(len < 9) return;

    if(flag_byte) {
        uint8_t xor_byte = raw[7];
        for(int i = 1; i < 7; i++) {
            raw[i] ^= xor_byte;
        }
    } else {
        uint8_t xor_byte = raw[6];
        for(int i = 1; i < 6; i++) {
            raw[i] ^= xor_byte;
        }
        raw[7] ^= xor_byte;
    }

    uint8_t b6 = raw[6];
    uint8_t b7 = raw[7];
    raw[6] = (b6 & 0xAA) | (b7 & 0x55);
    raw[7] = (b7 & 0xAA) | (b6 & 0x55);
}

static void ford_v1_encode_inverse_block(uint8_t block[9]) {
    uint8_t sum = 0;
    for(size_t i = 1; i <= 7; i++) {
        sum = (uint8_t)(sum + block[i]);
    }

    const uint8_t p6 = block[6];
    const uint8_t p7 = block[7];
    const uint8_t post6 = (uint8_t)((p6 & 0xAAU) | (p7 & 0x55U));
    const uint8_t post7 = (uint8_t)((p7 & 0xAAU) | (p6 & 0x55U));
    const uint8_t xorv = (uint8_t)(post6 ^ post7);

    uint8_t xor_byte;
    if((__builtin_popcount((unsigned int)sum) & 1) != 0) {
        block[6] = xorv;
        block[7] = post7;
        xor_byte = post7;
    } else {
        block[6] = post6;
        block[7] = xorv;
        xor_byte = post6;
    }

    for(size_t i = 1; i <= 5; i++) {
        block[i] ^= xor_byte;
    }
}

static void ford_v1_encode_air_9bytes(const uint8_t* plain9, uint8_t* air9_out) {
    uint8_t block[9];
    memcpy(block, plain9, 9);
    ford_v1_encode_inverse_block(block);
    memcpy(air9_out, block, 9);
}

static bool ford_v1_plain_from_air(const uint8_t air9[9], uint8_t plain9_out[9]) {
    for(uint8_t flag = 0; flag < 2; flag++) {
        uint8_t cand[9];
        memcpy(cand, air9, 9);
        ford_v1_decode_with_flag(cand, 9, flag);
        uint8_t reair[9];
        ford_v1_encode_air_9bytes(cand, reair);
        if(memcmp(reair, air9, 9) == 0) {
            memcpy(plain9_out, cand, 9);
            return true;
        }
    }
    return false;
}

static bool ford_v1_extract_plain_from_raw(
    const uint8_t* raw17_in,
    uint8_t* plain9_out,
    uint8_t* raw17_canonical_out_opt) {
    uint8_t raw[FORD_V1_DATA_BYTES];
    memcpy(raw, raw17_in, FORD_V1_DATA_BYTES);

    uint16_t calc_crc = ford_v1_crc16(&raw[3], 12);
    uint16_t recv_crc = ((uint16_t)raw[15] << 8) | raw[16];

    if(recv_crc != calc_crc) {
        memcpy(raw, raw17_in, FORD_V1_DATA_BYTES);
        for(size_t i = 0; i < FORD_V1_DATA_BYTES; i++) {
            raw[i] = (uint8_t)~raw[i];
        }
        calc_crc = ford_v1_crc16(&raw[3], 12);
        recv_crc = ((uint16_t)raw[15] << 8) | raw[16];
    }

    if(recv_crc != calc_crc) {
        return false;
    }

    for(uint8_t flag = 0; flag < 2; flag++) {
        uint8_t cand[9];
        memcpy(cand, &raw[6], 9);
        ford_v1_decode_with_flag(cand, 9, flag);
        if((cand[3] == raw[5]) && (cand[4] == raw[6])) {
            memcpy(plain9_out, cand, 9);
            if(raw17_canonical_out_opt) {
                memcpy(raw17_canonical_out_opt, raw, FORD_V1_DATA_BYTES);
            }
            return true;
        }
    }

    if(ford_v1_plain_from_air(&raw[6], plain9_out)) {
        if(raw17_canonical_out_opt) {
            memcpy(raw17_canonical_out_opt, raw, FORD_V1_DATA_BYTES);
        }
        return true;
    }

    return false;
}

static uint16_t ford_v1_crc16_from_key3_bytes(const uint8_t key3[4]) {
    uint32_t crc = ((uint32_t)key3[0] << 24) | ((uint32_t)key3[1] << 16) |
                   ((uint32_t)key3[2] << 8) | (uint32_t)key3[3];
    if((crc & 0xFFFFU) == 0U && (crc >> 16) != 0U) {
        return __builtin_bswap16((uint16_t)(crc >> 16));
    }
    return (uint16_t)(crc & 0xFFFFU);
}

static uint64_t ford_v1_u64_from_be_key8(const uint8_t key8[8]) {
    uint64_t v = 0;
    for(int i = 0; i < 8; i++) {
        v = (v << 8) | (uint64_t)key8[i];
    }
    return v;
}

static uint64_t ford_v1_u64_legacy_halves_bswap(const uint8_t key8[8]) {
    uint64_t stored = ford_v1_u64_from_be_key8(key8);
    return ((uint64_t)__builtin_bswap32((uint32_t)(stored >> 32)) << 32) |
           (uint64_t)__builtin_bswap32((uint32_t)(stored & 0xFFFFFFFFU));
}

static void
    ford_v1_raw14_from_internal_keys(uint64_t key1, uint64_t data2, uint8_t raw14_out[14]) {
    for(int i = 0; i < 7; i++) {
        raw14_out[i] = (uint8_t)((key1 >> (48 - i * 8)) & 0xFFU);
    }
    for(int i = 0; i < 8; i++) {
        raw14_out[7 + i] = (uint8_t)((data2 >> (56 - i * 8)) & 0xFFU);
    }
}

static bool ford_v1_keys_file_to_canonical_raw(
    const uint8_t key1b[8],
    const uint8_t key2b[8],
    uint16_t crc16,
    uint64_t* key1_out,
    uint64_t* key2_out,
    uint8_t raw17[FORD_V1_DATA_BYTES],
    uint8_t plain9[9]) {
    for(unsigned attempt = 0; attempt < 2; attempt++) {
        uint64_t k1 = (attempt == 0) ? ford_v1_u64_from_be_key8(key1b) :
                                       ford_v1_u64_legacy_halves_bswap(key1b);
        uint64_t k2 = (attempt == 0) ? ford_v1_u64_from_be_key8(key2b) :
                                       ford_v1_u64_legacy_halves_bswap(key2b);
        *key1_out = k1;
        *key2_out = k2;
        ford_v1_raw14_from_internal_keys(k1, k2, raw17);
        raw17[15] = (uint8_t)((crc16 >> 8) & 0xFFU);
        raw17[16] = (uint8_t)(crc16 & 0xFFU);
        if(ford_v1_extract_plain_from_raw(raw17, plain9, raw17)) {
            return true;
        }
    }
    *key1_out = ford_v1_u64_from_be_key8(key1b);
    *key2_out = ford_v1_u64_from_be_key8(key2b);
    ford_v1_raw14_from_internal_keys(*key1_out, *key2_out, raw17);
    raw17[15] = (uint8_t)((crc16 >> 8) & 0xFFU);
    raw17[16] = (uint8_t)(crc16 & 0xFFU);
    return false;
}

static void
    ford_v1_plain_apply_fields(uint8_t* plain9, uint32_t serial, uint8_t btn, uint32_t cnt) {
    uint8_t chk = (uint8_t)(plain9[8] - plain9[6] - plain9[7] - plain9[5]);
    plain9[0] = (uint8_t)(serial & 0xFFU);
    plain9[1] = (uint8_t)((serial >> 24) & 0xFFU);
    plain9[2] = (uint8_t)((serial >> 16) & 0xFFU);
    plain9[3] = (uint8_t)((serial >> 8) & 0xFFU);
    plain9[5] = (uint8_t)(((btn & 0x0FU) << 4) | ((cnt >> 16) & 0x0FU));
    plain9[6] = (uint8_t)((cnt >> 8) & 0xFFU);
    plain9[7] = (uint8_t)(cnt & 0xFFU);
    plain9[8] = (uint8_t)(chk + plain9[7] + plain9[6] + plain9[5]);
}

static void ford_v1_encoder_rebuild_raw_from_plain(uint8_t* raw17, const uint8_t* plain9) {
    uint8_t air9[9];
    ford_v1_encode_air_9bytes(plain9, air9);
    memcpy(&raw17[6], air9, 9);
    uint16_t c = ford_v1_crc16(&raw17[3], 12);
    raw17[15] = (uint8_t)(c >> 8);
    raw17[16] = (uint8_t)(c & 0xFF);
}

static void ford_v1_encoder_keys_from_raw(SubGhzProtocolEncoderFordV1* instance) {
    uint8_t* raw = instance->raw_tx;
    uint64_t key1 = 0;
    for(int i = 0; i < 7; i++) {
        key1 = (key1 << 8) | raw[i];
    }
    uint64_t key2 = 0;
    for(int i = 0; i < 8; i++) {
        key2 = (key2 << 8) | raw[7 + i];
    }
    instance->generic.data = key1;
    instance->data2 = key2;
    instance->crc_calc = (uint16_t)(((uint16_t)raw[15] << 8) | raw[16]);
}

static uint32_t ford_v1_encoder_preset_hop_read_stub(void) {
    return 0;
}

static void ford_v1_encoder_apply_counter_cap(uint32_t* cnt_p, uint32_t cap) {
    uint32_t ee = ford_v1_encoder_preset_hop_read_stub();
    if(ee == 0x80000001u) {
        uint32_t u3 = *cnt_p;
        uint32_t r2 = u3 + 1u;
        if(r2 > cap) {
            *cnt_p = 0;
            return;
        }
        if(u3 == 0u) {
            *cnt_p = r2;
            return;
        }
        uint32_t capm1 = cap - 1u;
        if(u3 == capm1) {
            *cnt_p = capm1;
            return;
        }
        *cnt_p = r2;
        return;
    }
    if(subghz_block_generic_global_counter_override_get(cnt_p)) {
        return;
    }
    uint32_t sum = ee + *cnt_p;
    if(sum <= cap) {
        *cnt_p = sum;
        return;
    }
    *cnt_p = 0;
}

static void ford_v1_encoder_patch_key1_low_bits(SubGhzProtocolEncoderFordV1* instance) {
    uint64_t k = instance->generic.data;
    uint32_t lo = (uint32_t)(k & 0xFFFFFFFFULL);
    lo = (lo & 0xFF00FFFFU) | 0x80000U;
    instance->generic.data = (k & 0xFFFFFFFF00000000ULL) | (uint64_t)lo;
}

static void ford_v1_encoder_build_burst(SubGhzProtocolEncoderFordV1* instance, uint8_t burst_idx) {
    LevelDuration* const upload = instance->encoder.upload;
    uint8_t pkt[FORD_V1_DATA_BYTES];
    memcpy(pkt, instance->raw_tx, FORD_V1_DATA_BYTES);

    pkt[4] = ford_v1_encoder_burst_pkt4_vals[burst_idx];
    uint16_t crcw = ford_v1_crc16(&pkt[3], 12);
    pkt[15] = (uint8_t)(crcw >> 8);
    pkt[16] = (uint8_t)(crcw & 0xFFU);

    size_t index = 0;
    for(size_t i = 0; i < FORD_V1_ENC_PREAMBLE_PAIRS; i++) {
        upload[index++] = level_duration_make(true, FORD_V1_ENC_SYNC_LONG_US);
        upload[index++] = level_duration_make(false, FORD_V1_ENC_SYNC_LONG_US);
    }
    upload[index++] = level_duration_make(true, FORD_V1_ENC_SYNC_LONG_US);
    upload[index++] = level_duration_make(false, FORD_V1_ENC_SYNC_SHORT_US);

    for(size_t by = 0; by < FORD_V1_DATA_BYTES; by++) {
        uint8_t b = pkt[by];
        for(int bit_i = 7; bit_i >= 0; bit_i--) {
            bool bit = ((b >> bit_i) & 1U) != 0U;
            upload[index++] = level_duration_make(bit, FORD_V1_ENC_SYNC_SHORT_US);
            upload[index++] = level_duration_make(!bit, FORD_V1_ENC_SYNC_SHORT_US);
        }
    }

    upload[index++] = level_duration_make(
        false,
        (burst_idx + 1U == FORD_V1_ENC_BURST_COUNT) ? FORD_V1_ENC_GAP_LAST_US :
                                                      FORD_V1_ENC_GAP_REPEAT_US);
    instance->encoder.size_upload = FORD_V1_ENC_BURST_LD_COUNT;
    instance->encoder.front = 0;
}

void* subghz_protocol_encoder_ford_v1_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderFordV1* instance = calloc(1, sizeof(SubGhzProtocolEncoderFordV1));
    furi_check(instance);
    instance->base.protocol = &ford_protocol_v1;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = 10;
    instance->encoder.front = 0;
    instance->encoder.is_running = false;
    instance->burst_idx = 0;
    instance->encoder.upload = malloc(FORD_V1_ENC_BURST_LD_COUNT * sizeof(LevelDuration));
    furi_check(instance->encoder.upload);
    return instance;
}

void subghz_protocol_encoder_ford_v1_free(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderFordV1* instance = context;
    if(instance->encoder.upload) {
        free(instance->encoder.upload);
        instance->encoder.upload = NULL;
    }
    free(instance);
}

void subghz_protocol_encoder_ford_v1_stop(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderFordV1* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
}

LevelDuration subghz_protocol_encoder_ford_v1_yield(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderFordV1* instance = context;
    if(instance->encoder.repeat == 0 || !instance->encoder.is_running || instance->encoder.size_upload == 0) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }
    LevelDuration ret = instance->encoder.upload[instance->encoder.front];
    if(++instance->encoder.front == instance->encoder.size_upload) {
        instance->encoder.repeat--;
        instance->encoder.front = 0;
    }
    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_ford_v1_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderFordV1* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    instance->encoder.repeat = 10;
    instance->plain_valid = 0;
    instance->burst_idx = 0;

    do {
        {
            flipper_format_rewind(flipper_format);
            FuriString* tmp = furi_string_alloc();
            bool name_ok = false;
            if(flipper_format_read_string(flipper_format, "Protocol", tmp)) {
                if(furi_string_equal_str(tmp, instance->base.protocol->name)) {
                    name_ok = true;
                }
            }
            furi_string_free(tmp);
            if(!name_ok) break;
        }

        flipper_format_rewind(flipper_format);
        uint8_t key1_bytes[8] = {0};
        if(!flipper_format_read_hex(flipper_format, "Key", key1_bytes, 8)) {
            break;
        }
        flipper_format_rewind(flipper_format);
        uint8_t key2_bytes[8] = {0};
        if(!flipper_format_read_hex(flipper_format, "Key_2", key2_bytes, 8)) {
            break;
        }
        flipper_format_rewind(flipper_format);
        uint8_t key3_bytes[4] = {0};
        if(!flipper_format_read_hex(flipper_format, "Key_3", key3_bytes, 4)) {
            break;
        }

        uint16_t crc16 = ford_v1_crc16_from_key3_bytes(key3_bytes);
        instance->crc_calc = crc16;

        bool keys_ok = ford_v1_keys_file_to_canonical_raw(
            key1_bytes,
            key2_bytes,
            crc16,
            &instance->generic.data,
            &instance->data2,
            instance->raw_tx,
            instance->plain9);

        instance->plain_valid = keys_ok ? 1U : 0U;
        instance->encryption_supported = instance->plain_valid;
        if(keys_ok) {
            ford_v1_encoder_keys_from_raw(instance);
            instance->crc_calc =
                (uint16_t)(((uint16_t)instance->raw_tx[15] << 8) | instance->raw_tx[16]);
        }

        if(!instance->encryption_supported || !instance->plain_valid) {
            break;
        }

        flipper_format_rewind(flipper_format);
        SubGhzProtocolStatus g = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_ford_v1_const.min_count_bit_for_found);
        if(g != SubGhzProtocolStatusOk) {
            ret = g;
            break;
        }

        ford_v1_encoder_keys_from_raw(instance);

        {
            uint32_t serial = UINT32_MAX;
            uint32_t btn = UINT32_MAX;
            uint32_t cnt = UINT32_MAX;
            flipper_format_rewind(flipper_format);
            if(!flipper_format_read_uint32(flipper_format, "Serial", &serial, 1))
                serial = UINT32_MAX;
            flipper_format_rewind(flipper_format);
            if(!flipper_format_read_uint32(flipper_format, "Btn", &btn, 1))
                btn = UINT32_MAX;
            flipper_format_rewind(flipper_format);
            if(!flipper_format_read_uint32(flipper_format, "Cnt", &cnt, 1))
                cnt = UINT32_MAX;
            if(serial == UINT32_MAX || btn == UINT32_MAX || cnt == UINT32_MAX) break;

            {
                const uint8_t original_btn = (uint8_t)(btn & 0x0FU);
                if(subghz_custom_btn_get_original() == 0) {
                    subghz_custom_btn_set_original(original_btn);
                }
                subghz_custom_btn_set_max(4);
                uint8_t custom_btn_id = subghz_custom_btn_get();
                switch(custom_btn_id) {
                case SUBGHZ_CUSTOM_BTN_UP:    btn = 0x02U; break;
                case SUBGHZ_CUSTOM_BTN_RIGHT: btn = 0x04U; break;
                case SUBGHZ_CUSTOM_BTN_DOWN:  btn = 0x08U; break;
                case SUBGHZ_CUSTOM_BTN_LEFT:  btn = 0x01U; break;
                case SUBGHZ_CUSTOM_BTN_OK:    btn = original_btn; break;
                default:                      btn = original_btn; break;
                }
            }

            instance->generic.serial = serial;
            instance->generic.btn = (uint8_t)(btn & 0x0FU);
            instance->generic.cnt = cnt & 0xFFFFFU;
        }

        {
            uint8_t btn_rf = (uint8_t)(instance->generic.btn & 0x0FU);

            ford_v1_encoder_apply_counter_cap(&instance->generic.cnt, 0xFFFFFU);

            uint8_t work[9];
            memcpy(work, instance->plain9, 9);
            ford_v1_plain_apply_fields(
                work, instance->generic.serial, btn_rf, instance->generic.cnt & 0xFFFFFU);
            memcpy(instance->plain9, work, 9);
            ford_v1_encoder_rebuild_raw_from_plain(instance->raw_tx, work);
            ford_v1_encoder_keys_from_raw(instance);
        }

        ford_v1_encoder_patch_key1_low_bits(instance);

        {
            flipper_format_rewind(flipper_format);
            uint32_t repeat_val = 0;
            instance->encoder.repeat =
                flipper_format_read_uint32(flipper_format, "Repeat", &repeat_val, 1) ? repeat_val :
                                                                                       10;
        }

        ford_v1_encoder_build_burst(instance, instance->burst_idx);

        {
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_uint32(
                flipper_format, "Serial", &instance->generic.serial, 1);
            uint32_t btn_store = instance->generic.btn;
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_uint32(flipper_format, "Btn", &btn_store, 1);
            uint32_t cnt_store = instance->generic.cnt;
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_uint32(flipper_format, "Cnt", &cnt_store, 1);
        }

        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

static const SubGhzProtocolEncoder enc_ford_v1_vt = {
    .alloc = subghz_protocol_encoder_ford_v1_alloc,
    .free = subghz_protocol_encoder_ford_v1_free,
    .deserialize = subghz_protocol_encoder_ford_v1_deserialize,
    .stop = subghz_protocol_encoder_ford_v1_stop,
    .yield = subghz_protocol_encoder_ford_v1_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol ford_protocol_v1 = {
    .name = FORD_PROTOCOL_V1_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_ford_v1_vt,
};

static const SubGhzEncodePluginEntry enc_ford_v1_entry = {.name = FORD_PROTOCOL_V1_NAME, .encoder = &enc_ford_v1_vt};
static const FlipperAppPluginDescriptor enc_ford_v1_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_ford_v1_entry};
const FlipperAppPluginDescriptor* enc_ford_v1_ep(void) { return &enc_ford_v1_descriptor; }
