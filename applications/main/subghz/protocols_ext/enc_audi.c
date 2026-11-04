/* enc_audi.fal : SD encoder for audi (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_audi enc_audi__subghz_protocol_audi
#define subghz_protocol_encoder_audi_alloc enc_audi__subghz_protocol_encoder_audi_alloc
#define subghz_protocol_encoder_audi_deserialize enc_audi__subghz_protocol_encoder_audi_deserialize
#define subghz_protocol_encoder_audi_stop enc_audi__subghz_protocol_encoder_audi_stop
#define subghz_protocol_encoder_common_free enc_audi__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_yield enc_audi__subghz_protocol_encoder_common_yield
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/audi.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <furi.h>
#include <furi_hal.h>
#include <string.h>
#include <lib/subghz/protocols/common.h>

#define AUDI_TE_SHORT     550U
#define AUDI_TE_LONG      1100U
#define AUDI_HEADER_HIGH  2500U
#define AUDI_HEADER_LOW   850U
#define AUDI_GAP_US       850U
#define AUDI_WIRE_BYTES   8U
#define AUDI_WIRE_BITS    64U
#define AUDI_REPEATS      5U
#define AUDI_SCRAMBLE_ROUNDS 4U
#define AUDI_UPLOAD_CAPACITY ((2U + (AUDI_WIRE_BITS * 2U) + 1U) * AUDI_REPEATS)
#define AUDI_DEFAULT_REPEAT AUDI_REPEATS
#define AUDI_DATA_FIELD "Data"
#define AUDI_BTN_0 0x01U
#define AUDI_BTN_1 0x02U
#define AUDI_BTN_2 0x04U

struct SubGhzProtocolEncoderAudi {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t raw_data[AUDI_WIRE_BYTES]; // on-wire (scrambled) 8 bytes
};

// ---------------------------------------------------------------------------
// Scramble / parity engine (@ 0xf8fc). Operates in place on {b3,b4,b5}.
// ---------------------------------------------------------------------------

static inline uint8_t audi_bit(uint8_t x, uint8_t n);
// P = bit3(b3) ^ bit4(b4) ^ bit6(b4) ^ bit7(b4) ^ bit4(b5) ^ bit7(b5)
static uint8_t audi_parity(uint8_t b3, uint8_t b4, uint8_t b5);
// One forward scramble iteration over block[0..2] = {b3,b4,b5}.
static void audi_scramble_iteration(uint8_t block[3]);
// One inverse scramble iteration. Returns true if the recovered block is
// self-consistent (recomputed parity matches the injected seed P == o0 bit0).
static bool audi_unscramble_iteration(uint8_t block[3]);
// Forward: scramble {b3,b4,b5} in place, 4 iterations, then checksum b7.
static void audi_encrypt(uint8_t raw[AUDI_WIRE_BYTES]);
// Inverse: un-scramble {b3,b4,b5} in place, 4 iterations. Returns parity_ok.
static bool audi_decrypt_block(uint8_t raw[AUDI_WIRE_BYTES]);
// ---------------------------------------------------------------------------
// Field helpers. raw_data[] holds the on-wire (scrambled) bytes; the plaintext
// rolling bytes are obtained on demand via audi_decrypt_block on a copy.
// ---------------------------------------------------------------------------

static uint32_t audi_uid(const uint8_t raw[AUDI_WIRE_BYTES]);
static bool audi_frame_valid(const uint8_t raw[AUDI_WIRE_BYTES]);
static void audi_decode_fields(SubGhzBlockGeneric* generic, const uint8_t raw[AUDI_WIRE_BYTES]);
// ---------------------------------------------------------------------------
// Encoder: rebuild the exact TX wire.
//   Per repeat (x5):
//     HIGH 2500 -> LOW 850 (header/sync)
//     64 PWM bits LSB-first:
//        bit1 -> HIGH 1100 , LOW 550
//        bit0 -> HIGH 550  , LOW 1100
//     LOW 850 inter-repeat gap
// The stored raw_data are the on-wire (scrambled) bytes, so replay reproduces
// the wire exactly. A D-pad button change re-derives the whole frame: set b6,
// advance the plaintext rolling counter (b3/b4), re-scramble, re-checksum.
// ---------------------------------------------------------------------------

static size_t audi_append_data_pairs(LevelDuration* upload, size_t index, const uint8_t raw[AUDI_WIRE_BYTES]);
static bool audi_encoder_build_upload(SubGhzProtocolEncoderAudi* instance);
// Rebuild the on-wire raw from a chosen button + plaintext rolling counter.
// Keeps UID (b0..b2) and b5 plaintext as captured; only b3/b4 carry the counter.
static void audi_reencode(SubGhzProtocolEncoderAudi* instance, uint8_t new_btn, uint16_t new_cnt);

// ---------------------------------------------------------------------------
// Scramble / parity engine (@ 0xf8fc). Operates in place on {b3,b4,b5}.
// ---------------------------------------------------------------------------

static inline uint8_t audi_bit(uint8_t x, uint8_t n) {
    return (uint8_t)((x >> n) & 1U);
}

// P = bit3(b3) ^ bit4(b4) ^ bit6(b4) ^ bit7(b4) ^ bit4(b5) ^ bit7(b5)
static uint8_t audi_parity(uint8_t b3, uint8_t b4, uint8_t b5) {
    return (uint8_t)(
        audi_bit(b3, 3) ^ audi_bit(b4, 4) ^ audi_bit(b4, 6) ^ audi_bit(b4, 7) ^ audi_bit(b5, 4) ^
        audi_bit(b5, 7));
}

// One forward scramble iteration over block[0..2] = {b3,b4,b5}.
static void audi_scramble_iteration(uint8_t block[3]) {
    const uint8_t p = audi_parity(block[0], block[1], block[2]);
    uint8_t carry = p;
    for(size_t i = 0; i < 3U; i++) {
        const uint8_t in = block[i];
        block[i] = (uint8_t)(((in << 1) & 0xFEU) | carry);
        carry = (uint8_t)((in >> 7) & 1U);
    }
    // The final carry (bit7 of the last input, i.e. old bit7(b5)) is dropped;
    // it is recoverable on decode via the parity relation (see below).
}

// One inverse scramble iteration. Returns true if the recovered block is
// self-consistent (recomputed parity matches the injected seed P == o0 bit0).
static bool audi_unscramble_iteration(uint8_t block[3]) {
    const uint8_t o0 = block[0];
    const uint8_t o1 = block[1];
    const uint8_t o2 = block[2];

    // The parity P injected as the first carry equals bit0 of the first output.
    const uint8_t p = (uint8_t)(o0 & 1U);

    // Recover all bits except in2 bit7:
    //   in0 = (o0 >> 1) | (o1 bit0 << 7)
    //   in1 = (o1 >> 1) | (o2 bit0 << 7)
    //   in2 = (o2 >> 1) | (in2_bit7 << 7)
    const uint8_t in0 = (uint8_t)(((o0 >> 1) & 0x7FU) | ((o1 & 1U) << 7));
    const uint8_t in1 = (uint8_t)(((o1 >> 1) & 0x7FU) | ((o2 & 1U) << 7));
    uint8_t in2_low7 = (uint8_t)((o2 >> 1) & 0x7FU);

    // Recover in2 bit7 from the parity relation:
    //   P = bit3(in0) ^ bit4(in1) ^ bit6(in1) ^ bit7(in1) ^ bit4(in2) ^ bit7(in2)
    // => bit7(in2) = P ^ bit3(in0) ^ bit4(in1) ^ bit6(in1) ^ bit7(in1) ^ bit4(in2)
    const uint8_t known = (uint8_t)(
        audi_bit(in0, 3) ^ audi_bit(in1, 4) ^ audi_bit(in1, 6) ^ audi_bit(in1, 7) ^
        audi_bit(in2_low7, 4));
    const uint8_t in2_bit7 = (uint8_t)(p ^ known);
    const uint8_t in2 = (uint8_t)(in2_low7 | (in2_bit7 << 7));

    block[0] = in0;
    block[1] = in1;
    block[2] = in2;

    // Self-check: recomputing P on the recovered plaintext must equal the seed.
    // This is guaranteed by construction (we solved for in2 bit7 to make it so),
    // but we return it so the decoder can flag corrupt captures.
    return audi_parity(in0, in1, in2) == p;
}

// Forward: scramble {b3,b4,b5} in place, 4 iterations, then checksum b7.
static void audi_encrypt(uint8_t raw[AUDI_WIRE_BYTES]) {
    uint8_t block[3] = {raw[3], raw[4], raw[5]};
    for(uint8_t r = 0; r < AUDI_SCRAMBLE_ROUNDS; r++) {
        audi_scramble_iteration(block);
    }
    raw[3] = block[0];
    raw[4] = block[1];
    raw[5] = block[2];

    uint32_t sum = 0U;
    for(size_t i = 0; i < 7U; i++) sum += raw[i];
    raw[7] = (uint8_t)(sum & 0xFFU);
}

// Inverse: un-scramble {b3,b4,b5} in place, 4 iterations. Returns parity_ok.
static bool audi_decrypt_block(uint8_t raw[AUDI_WIRE_BYTES]) {
    uint8_t block[3] = {raw[3], raw[4], raw[5]};
    bool ok = true;
    for(uint8_t r = 0; r < AUDI_SCRAMBLE_ROUNDS; r++) {
        ok = audi_unscramble_iteration(block) && ok;
    }
    raw[3] = block[0];
    raw[4] = block[1];
    raw[5] = block[2];
    return ok;
}

// ---------------------------------------------------------------------------
// Field helpers. raw_data[] holds the on-wire (scrambled) bytes; the plaintext
// rolling bytes are obtained on demand via audi_decrypt_block on a copy.
// ---------------------------------------------------------------------------

static uint32_t audi_uid(const uint8_t raw[AUDI_WIRE_BYTES]) {
    return ((uint32_t)raw[0] << 16U) | ((uint32_t)raw[1] << 8U) | raw[2];
}

static bool audi_frame_valid(const uint8_t raw[AUDI_WIRE_BYTES]) {
    const uint32_t uid = audi_uid(raw);
    return uid != 0U && uid != 0xFFFFFFU;
}

static void audi_decode_fields(SubGhzBlockGeneric* generic, const uint8_t raw[AUDI_WIRE_BYTES]) {
    generic->serial = audi_uid(raw);
    generic->btn = raw[6];
    // Rolling payload (plaintext) forms the counter view.
    uint8_t plain[AUDI_WIRE_BYTES];
    memcpy(plain, raw, AUDI_WIRE_BYTES);
    audi_decrypt_block(plain);
    generic->cnt = (uint16_t)(((uint16_t)plain[3] << 8U) | plain[4]);
    generic->data_count_bit = AUDI_WIRE_BITS;
    // 64-bit on-wire payload fits generic.data (MSB-first over b0..b7) for hash.
    uint64_t data = 0U;
    for(size_t i = 0; i < AUDI_WIRE_BYTES; i++) data = (data << 8U) | raw[i];
    generic->data = data;
}

// ---------------------------------------------------------------------------
// Encoder: rebuild the exact TX wire.
//   Per repeat (x5):
//     HIGH 2500 -> LOW 850 (header/sync)
//     64 PWM bits LSB-first:
//        bit1 -> HIGH 1100 , LOW 550
//        bit0 -> HIGH 550  , LOW 1100
//     LOW 850 inter-repeat gap
// The stored raw_data are the on-wire (scrambled) bytes, so replay reproduces
// the wire exactly. A D-pad button change re-derives the whole frame: set b6,
// advance the plaintext rolling counter (b3/b4), re-scramble, re-checksum.
// ---------------------------------------------------------------------------

static size_t audi_append_data_pairs(LevelDuration* upload, size_t index, const uint8_t raw[AUDI_WIRE_BYTES]) {
    for(uint16_t bit_index = 0U; bit_index < AUDI_WIRE_BITS; bit_index++) {
        const bool bit = ((raw[bit_index >> 3U] >> (bit_index & 7U)) & 1U) != 0U;
        if(bit) {
            upload[index++] = level_duration_make(true, AUDI_TE_LONG);
            upload[index++] = level_duration_make(false, AUDI_TE_SHORT);
        } else {
            upload[index++] = level_duration_make(true, AUDI_TE_SHORT);
            upload[index++] = level_duration_make(false, AUDI_TE_LONG);
        }
    }
    return index;
}

static bool audi_encoder_build_upload(SubGhzProtocolEncoderAudi* instance) {
    furi_check(instance);
    LevelDuration* upload = instance->encoder.upload;
    if(!upload) return false;

    size_t index = 0U;
    for(uint8_t rep = 0U; rep < AUDI_REPEATS; rep++) {
        // Header / sync.
        upload[index++] = level_duration_make(true, AUDI_HEADER_HIGH);
        upload[index++] = level_duration_make(false, AUDI_HEADER_LOW);
        // 64 PWM data bits LSB-first.
        index = audi_append_data_pairs(upload, index, instance->raw_data);
        // Inter-repeat gap.
        upload[index++] = level_duration_make(false, AUDI_GAP_US);
    }

    instance->encoder.size_upload = index;
    instance->encoder.front = 0U;
    return true;
}

// Rebuild the on-wire raw from a chosen button + plaintext rolling counter.
// Keeps UID (b0..b2) and b5 plaintext as captured; only b3/b4 carry the counter.
static void audi_reencode(SubGhzProtocolEncoderAudi* instance, uint8_t new_btn, uint16_t new_cnt) {
    uint8_t plain[AUDI_WIRE_BYTES];
    memcpy(plain, instance->raw_data, AUDI_WIRE_BYTES);
    audi_decrypt_block(plain); // recover plaintext b3,b4,b5

    plain[3] = (uint8_t)((new_cnt >> 8U) & 0xFFU);
    plain[4] = (uint8_t)(new_cnt & 0xFFU);
    plain[6] = new_btn;

    audi_encrypt(plain); // re-scramble {b3,b4,b5} + checksum b7
    memcpy(instance->raw_data, plain, AUDI_WIRE_BYTES);
}

/* vendored from lib/subghz/protocols/common.c (not exported) */
void subghz_protocol_encoder_common_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderCommon* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

/* vendored from lib/subghz/protocols/common.c (not exported) */
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

void* subghz_protocol_encoder_audi_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderAudi* instance = calloc(1, sizeof(SubGhzProtocolEncoderAudi));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_audi;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = AUDI_DEFAULT_REPEAT;
    instance->encoder.size_upload = 0U;
    instance->encoder.upload = NULL;
    instance->encoder.is_running = false;
    instance->encoder.front = 0U;
    return instance;
}

void subghz_protocol_encoder_audi_stop(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderAudi* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0U;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_audi_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    furi_check(flipper_format);
    SubGhzProtocolEncoderAudi* instance = context;

    instance->encoder.is_running = false;
    instance->encoder.front = 0U;

    flipper_format_rewind(flipper_format);
    FuriString* protocol_name = furi_string_alloc();
    bool protocol_ok = flipper_format_read_string(flipper_format, "Protocol", protocol_name) &&
                       furi_string_equal(protocol_name, instance->base.protocol->name);
    furi_string_free(protocol_name);
    if(!protocol_ok) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    uint32_t bit_count = 0U;
    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_uint32(flipper_format, "Bit", &bit_count, 1) ||
       bit_count != AUDI_WIRE_BITS) {
        return SubGhzProtocolStatusErrorValueBitCount;
    }
    instance->generic.data_count_bit = (uint16_t)bit_count;

    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(
           flipper_format, AUDI_DATA_FIELD, instance->raw_data, AUDI_WIRE_BYTES) ||
       !audi_frame_valid(instance->raw_data)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    instance->generic.serial = audi_uid(instance->raw_data);
    instance->generic.btn = instance->raw_data[6];
    {
        uint8_t plain[AUDI_WIRE_BYTES];
        memcpy(plain, instance->raw_data, AUDI_WIRE_BYTES);
        audi_decrypt_block(plain);
        instance->generic.cnt = (uint16_t)(((uint16_t)plain[3] << 8U) | plain[4]);
    }

    // [PROTOPIRATE_PORT] custom_btn support. Audi has 3 buttons:
    //   btn0=0x01 (Lock), btn1=0x02 (Unlock), btn2=0x04 (Trunk).
    // OK replays the captured frame byte-identically. A D-pad change re-derives
    // the whole frame (b6 + advanced rolling counter, re-scrambled + checksum),
    // so the emitted frame round-trips through this decoder.
    const uint8_t original_btn = instance->raw_data[6];
    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(original_btn);
    }
    subghz_custom_btn_set_max(3);
    const uint8_t custom_btn_id = subghz_custom_btn_get();
    uint8_t new_btn = original_btn;
    switch(custom_btn_id) {
    case SUBGHZ_CUSTOM_BTN_UP:   new_btn = AUDI_BTN_0; break;
    case SUBGHZ_CUSTOM_BTN_DOWN: new_btn = AUDI_BTN_1; break;
    case SUBGHZ_CUSTOM_BTN_LEFT: new_btn = AUDI_BTN_2; break;
    case SUBGHZ_CUSTOM_BTN_OK:
    default:                     new_btn = original_btn; break; // exact replay
    }

    if(new_btn != original_btn) {
        // Advance the plaintext rolling counter for a genuine (non-replay) press.
        uint32_t new_counter = instance->generic.cnt;
        uint32_t override_cnt = 0U;
        if(subghz_block_generic_global_counter_override_get(&override_cnt)) {
            new_counter = override_cnt;
        } else {
            new_counter += (uint32_t)furi_hal_subghz_get_rolling_counter_mult();
        }
        new_counter &= 0xFFFFU;
        audi_reencode(instance, new_btn, (uint16_t)new_counter);
        instance->generic.serial = audi_uid(instance->raw_data);
        instance->generic.btn = new_btn;
        instance->generic.cnt = (uint16_t)new_counter;
    }

    audi_decode_fields(&instance->generic, instance->raw_data);

    uint32_t repeat = AUDI_DEFAULT_REPEAT;
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Repeat", &repeat, 1);
    instance->encoder.repeat = (repeat == 0U) ? AUDI_DEFAULT_REPEAT : (size_t)repeat;

    if(instance->encoder.upload == NULL) {
        instance->encoder.upload = malloc(AUDI_UPLOAD_CAPACITY * sizeof(LevelDuration));
        furi_check(instance->encoder.upload);
    }
    if(!audi_encoder_build_upload(instance)) {
        return SubGhzProtocolStatusErrorEncoderGetUpload;
    }

    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

static const SubGhzProtocolEncoder enc_audi_vt = {
    .alloc = subghz_protocol_encoder_audi_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_audi_deserialize,
    .stop = subghz_protocol_encoder_audi_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_audi = {
    .name = AUDI_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 |
            SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_audi_vt,
};

static const SubGhzEncodePluginEntry enc_audi_entry = {.name = AUDI_PROTOCOL_NAME, .encoder = &enc_audi_vt};
static const FlipperAppPluginDescriptor enc_audi_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_audi_entry};
const FlipperAppPluginDescriptor* enc_audi_ep(void) { return &enc_audi_descriptor; }
