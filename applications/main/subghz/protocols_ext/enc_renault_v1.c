/* enc_renault_v1.fal : SD encoder for renault_v1 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define renault_v1_iv_to_u32 enc_renault_v1__renault_v1_iv_to_u32
#define subghz_protocol_encoder_renault_v1_alloc enc_renault_v1__subghz_protocol_encoder_renault_v1_alloc
#define subghz_protocol_encoder_renault_v1_deserialize enc_renault_v1__subghz_protocol_encoder_renault_v1_deserialize
#define subghz_protocol_renault_v1 enc_renault_v1__subghz_protocol_renault_v1
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/renault_v1.h>
#include <lib/subghz/protocols/renault_v1_crypto.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/common.h>

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
#define RENAULT_V1_CUSTOM_BTN_MAX  2 // D-pad custom buttons: this fob has only Lock(0x1) + Unlock(0x2)
#define RENAULT_V1_COUNTER_BITS    16 // rolling counter (push_clock) width, carved from the plaintext IV
#define RENAULT_V1_COUNTER_MAX     ((1u << RENAULT_V1_COUNTER_BITS) - 1)

enum {
    RENAULT_V1_DATA_FRAME_ENTRIES =
        2 + (RENAULT_V1_SYNC_BIT_COUNT + RENAULT_V1_DATA_1_SIZE + RENAULT_V1_DATA_2_SIZE) * 2,
    RENAULT_V1_TAIL_FRAME_ENTRIES =
        2 + (RENAULT_V1_SYNC_BIT_COUNT + RENAULT_V1_CHECK_BIT_COUNT) * 2 + 1,
    RENAULT_V1_UPLOAD_SIZE = RENAULT_V1_PRE_COUNT * 2 +
                             RENAULT_V1_DATA_REPEAT * RENAULT_V1_DATA_FRAME_ENTRIES +
                             RENAULT_V1_DATA_REPEAT * RENAULT_V1_TAIL_FRAME_ENTRIES,
};

struct SubGhzProtocolEncoderRenaultV1 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_renault_v1_const = {
    .te_short = 125, // Manchester short pulse
    .te_long = 250, // Manchester long pulse, 2× short
    .te_delta = 100, // Tolerance window for FEED
    .min_count_bit_for_found = 88, // Total data bits
};

static const uint8_t renault_v1_btns[] = {0x0, 0x1, 0x2, 0x4, 0x8};

static const uint8_t renault_v1_btn_map[RENAULT_V1_CUSTOM_BTN_MAX][5] = {
    {0x1, 0x1, 0x1, 0x1, 0x1}, // custom_id=1 (UP)   -> Lock
    {0x2, 0x2, 0x2, 0x2, 0x2}, // custom_id=2 (DOWN) -> Unlock
};

/* host bridge: live firmware functions/state this encoder needs (set by the loader) */
typedef struct {
    __typeof__(&subghz_protocol_renault_v1_encrypt) subghz_protocol_renault_v1_encrypt;
} enc_renault_v1_host_t;
static const void* enc_renault_v1_host_raw;
#define ENC_RENAULT_V1_HOST ((const enc_renault_v1_host_t*)enc_renault_v1_host_raw)
#define subghz_protocol_renault_v1_encrypt (ENC_RENAULT_V1_HOST->subghz_protocol_renault_v1_encrypt)

/* Derive serial and button from the payload. The 88-bit payload is 11 bytes; generic.data holds
 * bytes 0..7 MSB-first, so bytes 0..3 are the serial and byte 4 carries the button in its HIGH
 * nibble (measured across all six fobs: Lock=1, Unlock=2, matching Largus.cpp's `pushkey<<4`).
 *
 * Derived HERE rather than in feed() on purpose: subghz_block_generic_deserialize restores only the
 * bit count and Key, and decoder slots are reused WITHOUT reset() between file loads, so anything set
 * only while receiving would be stale or zero for a loaded .sub. This runs from serialize(),
 * deserialize() AND get_string(), which is every path that can display or re-encode. */
static void subghz_protocol_renault_v1_check_remote_controller(SubGhzBlockGeneric* instance);
/* One Manchester symbol pair. Which way round is NOT a matter of convention here: the decoder is
 * validated against real fobs, so the encoder is written to match it. Under this mapping the bits
 * recovered by the decoder are the COMPLEMENT of the ones written -- which is why the payload below
 * is emitted verbatim rather than inverted, and why RENAULT_V1_SYNC_WORD is written plain and still reads back
 * as 0x0001. Proven by round-tripping a captured frame through both, and by comparing the generated
 * waveform against the fob's own capture symbol for symbol. */
static size_t renault_v1_add_bit(LevelDuration* upload, size_t index, bool bit);
/* @p count bits of @p value, MSB first -- the order the fob transmits.
 *
 * This replaces six hand-unrolled copies that all read `bit_read(x, i - 1)`. That index is wrong
 * twice over: at i = 0 it shifts by -1, which is undefined behaviour (in practice a 0 bit), and the
 * remaining bits then come out LSB-ward, so the sync word went out as 0100...0 instead of 0000...1
 * and the payload was scrambled. Nothing a receiver could decode. */
static size_t
    renault_v1_add_bits(LevelDuration* upload, size_t index, uint64_t value, uint8_t count);
static bool subghz_protocol_encoder_renault_v1_get_upload(SubGhzProtocolEncoderRenaultV1* instance);

/* Derive serial and button from the payload. The 88-bit payload is 11 bytes; generic.data holds
 * bytes 0..7 MSB-first, so bytes 0..3 are the serial and byte 4 carries the button in its HIGH
 * nibble (measured across all six fobs: Lock=1, Unlock=2, matching Largus.cpp's `pushkey<<4`).
 *
 * Derived HERE rather than in feed() on purpose: subghz_block_generic_deserialize restores only the
 * bit count and Key, and decoder slots are reused WITHOUT reset() between file loads, so anything set
 * only while receiving would be stale or zero for a loaded .sub. This runs from serialize(),
 * deserialize() AND get_string(), which is every path that can display or re-encode. */
static void subghz_protocol_renault_v1_check_remote_controller(SubGhzBlockGeneric* instance) {
    instance->serial = (uint32_t)(instance->data >> 32);
    instance->btn = (uint8_t)((instance->data >> 28) & 0x0F);
    /* Offer the D-pad button swap ONLY once recovered: on an un-recovered signal the encoder can only
     * replay the captured payload (it cannot re-encrypt a different button), so a swap would send the
     * original button while the on-screen label shows the chosen one. set_max(0) hides the D-pad then.
     * status==1 only: a status of 2 (BF not found) is NOT a recovery, so it must not unlock the swap. */
    subghz_custom_btn_set_max((instance->status == 1) ? RENAULT_V1_CUSTOM_BTN_MAX : 0);
    /* The rolling counter is NOT in the raw ciphertext -- it is a plaintext field of the DECRYPTED IV
     * that recovery leaves in generic.seed, so it exists only once status is set. Host-checked over 44
     * consecutive presses of two fobs, this 16-bit field steps by exactly 1 per press (fob1 2A7A..2AA5,
     * fob3 1208..1233) while the fixed part holds. Deriving it here, from the one stored Seed, keeps
     * every display/serialize/deserialize path consistent and means the counter is never written to file.
     * Layout of the decrypted IV: iv0 low nibble = button, iv0 high nibble | iv1 | iv2 low nibble =
     * counter, iv2 high nibble | iv3 = fixed. status==1 only -- status 2 (BF not found) has no IV. */
    if(instance->status == 1) {
        const uint32_t iv = instance->seed;
        instance->cnt =
            ((iv >> 28) & 0x0F) | (((iv >> 16) & 0xFF) << 4) | (((iv >> 8) & 0x0F) << 12);
    } else {
        instance->cnt = 0;
    }
}

/* One Manchester symbol pair. Which way round is NOT a matter of convention here: the decoder is
 * validated against real fobs, so the encoder is written to match it. Under this mapping the bits
 * recovered by the decoder are the COMPLEMENT of the ones written -- which is why the payload below
 * is emitted verbatim rather than inverted, and why RENAULT_V1_SYNC_WORD is written plain and still reads back
 * as 0x0001. Proven by round-tripping a captured frame through both, and by comparing the generated
 * waveform against the fob's own capture symbol for symbol. */
static size_t renault_v1_add_bit(LevelDuration* upload, size_t index, bool bit) {
    const uint32_t te = subghz_protocol_renault_v1_const.te_short;
    upload[index++] = level_duration_make(bit, te);
    upload[index++] = level_duration_make(!bit, te);
    return index;
}

/* @p count bits of @p value, MSB first -- the order the fob transmits.
 *
 * This replaces six hand-unrolled copies that all read `bit_read(x, i - 1)`. That index is wrong
 * twice over: at i = 0 it shifts by -1, which is undefined behaviour (in practice a 0 bit), and the
 * remaining bits then come out LSB-ward, so the sync word went out as 0100...0 instead of 0000...1
 * and the payload was scrambled. Nothing a receiver could decode. */
static size_t
    renault_v1_add_bits(LevelDuration* upload, size_t index, uint64_t value, uint8_t count) {
    while(count) {
        count--;
        index = renault_v1_add_bit(upload, index, (value >> count) & 1);
    }
    return index;
}

static bool subghz_protocol_encoder_renault_v1_get_upload(SubGhzProtocolEncoderRenaultV1* instance) {
    furi_assert(instance);
    size_t index = 0;

    /* Resolve the button first (custom remap + global override); the re-encryption below then puts the
     * chosen button on air. The button lives in the high nibble of payload byte 4 and inside the
     * encrypted span, so it can only change together with a fresh ciphertext + CRC -- which the
     * re-encryption below does. */
    subghz_custom_btn_set_original_if_unset(instance->generic.btn);

    subghz_custom_btn_apply_map(
        &instance->generic.btn, subghz_custom_btn_get_original(), subghz_custom_btn_get(),
        renault_v1_btns, (const uint8_t*)renault_v1_btn_map, COUNT_OF(renault_v1_btns),
        RENAULT_V1_CUSTOM_BTN_MAX);

    subghz_block_generic_global_button_override_get(&instance->generic.btn);

    /* Rolling re-encryption: once the key is recovered (status set), each press goes on air as a FRESH
     * rolling code, not a replay. advance_counter applies a user's Set-Counter / Counter-BF override or,
     * failing that, steps the counter; the new 16-bit counter and the (possibly remapped) button are
     * folded back into the plaintext IV held in generic.seed, and the whole 11-byte payload + byte-10
     * CRC are regenerated. generic.seed is advanced too so the encoder's write-back persists it for the
     * next press. An un-recovered signal (status 0) keeps the static-replay path below. The frame is a
     * byte-exact match to what the real fob sends for that counter (see renault_v1_crypto.c).
     * status==1 only -- a status of 2 (BF not found) never recovered a seed, so re-encrypting from it
     * would roll a counter off garbage; it must fall through to the static replay. */
    if(instance->generic.status == 1) {
        subghz_block_generic_advance_counter(&instance->generic.cnt, RENAULT_V1_COUNTER_MAX);
        const uint16_t cnt = (uint16_t)(instance->generic.cnt & RENAULT_V1_COUNTER_MAX);
        const uint8_t button = (uint8_t)(instance->generic.btn & 0x0F);
        /* Plaintext IV: button in iv0 low nibble, 16-bit counter in iv0_hi|iv1|iv2_lo, Fix (per-fob
         * constant) kept from the stored Seed's iv2 high nibble + iv3. */
        const uint8_t iv[4] = {
            (uint8_t)(((cnt & 0x0F) << 4) | button),
            (uint8_t)((cnt >> 4) & 0xFF),
            (uint8_t)(((cnt >> 12) & 0x0F) | ((uint8_t)(instance->generic.seed >> 8) & 0xF0)),
            (uint8_t)(instance->generic.seed)};
        const uint8_t serial[4] = {
            (uint8_t)(instance->generic.data >> 56),
            (uint8_t)(instance->generic.data >> 48),
            (uint8_t)(instance->generic.data >> 40),
            (uint8_t)(instance->generic.data >> 32)};
        uint8_t pay[11];
        subghz_protocol_renault_v1_encrypt(serial, iv, pay);
        uint64_t d = 0, d2 = 0;
        for(int i = 0; i < 8; i++) d = (d << 8) | pay[i];
        for(int i = 8; i < 11; i++) d2 = (d2 << 8) | pay[i];
        instance->generic.data = d;
        instance->generic.data_2 = d2;
        instance->generic.seed = renault_v1_iv_to_u32(iv);
    }

    /* Preamble: one long run of alternating half-bits, sent ONCE ahead of the first frame.
     * HIGH first, matching the fobs (measured 508..511 pulses; RENAULT_V1_PRE_COUNT pairs gives 500). */
    for(int p = 0; p < RENAULT_V1_PRE_COUNT; p++) {
        instance->encoder.upload[index++] =
            level_duration_make(true, subghz_protocol_renault_v1_const.te_short);
        instance->encoder.upload[index++] =
            level_duration_make(false, subghz_protocol_renault_v1_const.te_short);
    }

    // RENAULT_V1_DATA_REPEAT frames of [transit gap][sync][88-bit payload], back to back, no preamble between.
    for(int r = 0; r < RENAULT_V1_DATA_REPEAT; r++) {
        instance->encoder.upload[index++] = level_duration_make(false, RENAULT_V1_LO_GAP);
        instance->encoder.upload[index++] = level_duration_make(true, RENAULT_V1_HI_GAP);
        index = renault_v1_add_bits(
            instance->encoder.upload, index, RENAULT_V1_SYNC_WORD, RENAULT_V1_SYNC_BIT_COUNT);
        index = renault_v1_add_bits(
            instance->encoder.upload, index, instance->generic.data, RENAULT_V1_DATA_1_SIZE);
        index = renault_v1_add_bits(
            instance->encoder.upload, index, instance->generic.data_2, RENAULT_V1_DATA_2_SIZE);
    }

    /* The 10-bit tail carries nothing of its own: it is a repack of payload bytes 4 and 5, exactly as
     * Largus.cpp builds package[11]/package[12]. Deriving it here is why the decoder does not bother
     * to receive it -- and it is why the old code was wrong to read generic.seed, a field nothing in
     * this protocol ever sets, which sent ten bits of uninitialised memory. */
    const uint8_t payload_b4 = (uint8_t)(instance->generic.data >> 24);
    const uint8_t payload_b5 = (uint8_t)(instance->generic.data >> 16);
    const uint8_t tail_hi = (uint8_t)(((payload_b4 << 4) & 0xF0) | (payload_b5 >> 4));
    const uint8_t tail_lo = (uint8_t)((payload_b5 << 4) & 0xF0);
    const uint16_t tail = (uint16_t)(((uint16_t)tail_hi << 2) | (tail_lo >> 6));

    // Tail frames: [transit gap][sync][10-bit tail][silence], repeated while the button is held.
    for(int t = 0; t < RENAULT_V1_DATA_REPEAT; t++) {
        instance->encoder.upload[index++] = level_duration_make(false, RENAULT_V1_LO_GAP);
        instance->encoder.upload[index++] = level_duration_make(true, RENAULT_V1_HI_GAP);
        index = renault_v1_add_bits(
            instance->encoder.upload, index, RENAULT_V1_SYNC_WORD, RENAULT_V1_SYNC_BIT_COUNT);
        index = renault_v1_add_bits(
            instance->encoder.upload, index, tail, RENAULT_V1_CHECK_BIT_COUNT);
        instance->encoder.upload[index++] = level_duration_make(false, RENAULT_V1_TAIL_REPEAT_GAP);
    }

    /* Bound check against the ALLOCATED capacity, not against size_upload -- size_upload is
     * overwritten with the produced length below, so comparing to it would compare a run against
     * itself on every call after the first. RENAULT_V1_UPLOAD_SIZE is the same expression the buffer
     * is allocated from, so this can only fire if the frame layout above changes without it. */
    furi_check(index <= RENAULT_V1_UPLOAD_SIZE);
    instance->encoder.size_upload = index;
    return true;
}

// Pack the 4-byte decrypted IV (MSB-first) into a 32-bit word; shared by bf_apply, the encoder re-seed
// and the bruteforce scene's "Seed:" display so the idiom lives once.
uint32_t renault_v1_iv_to_u32(const uint8_t iv[4]) {
    return ((uint32_t)iv[0] << 24) | ((uint32_t)iv[1] << 16) | ((uint32_t)iv[2] << 8) | iv[3];
}

void* subghz_protocol_encoder_renault_v1_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderRenaultV1* instance = malloc(sizeof(SubGhzProtocolEncoderRenaultV1));

    instance->base.protocol = &subghz_protocol_renault_v1;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 1;
    instance->encoder.size_upload = RENAULT_V1_UPLOAD_SIZE;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_renault_v1_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderRenaultV1* instance = context;
    SubGhzProtocolStatus res = SubGhzProtocolStatusError;
    do {
        if(subghz_block_generic_deserialize_check_count_bit(
            &instance->generic, flipper_format, subghz_protocol_renault_v1_const.min_count_bit_for_found) !=
           SubGhzProtocolStatusOk) {
            // FURI_LOG_E(TAG, "EGF"); //Encoder Deserialize: generic failed
            break;
        }
        if(!flipper_format_rewind(flipper_format)) {
            // FURI_LOG_E(TAG, "ERE1"); //Encoder Deserialize: rewind error 1
            break;
        }
        if(!subghz_block_read_hex(flipper_format, "Key", &instance->generic.data, sizeof(uint64_t))) {
            // FURI_LOG_E(TAG, "EMK"); //Encoder Deserialize: missing Key
            break;
        }
        if(!subghz_block_read_hex(
               flipper_format, "Key_2", &instance->generic.data_2, sizeof(uint64_t))) {
            // FURI_LOG_E(TAG, "EMK2"); //Encoder Deserialize: missing Key_2
            break;
        }
        /* Recovered IV (Seed) + marker -- optional, present only once a key was recovered. The encoder
         * needs the Seed to re-encrypt a fresh rolling code; without it (status 0) get_upload replays
         * the captured payload verbatim. Rewind before each so a missing field does not strand the read. */
        instance->generic.status = 0;
        instance->generic.seed = 0;
        if(flipper_format_rewind(flipper_format))
            subghz_block_read_hex(
                flipper_format, "Recovered", &instance->generic.status, sizeof(uint8_t));
        if(flipper_format_rewind(flipper_format))
            subghz_block_read_hex(
                flipper_format, "Seed", &instance->generic.seed, sizeof(uint32_t));
        if(!flipper_format_rewind(flipper_format)) break;
        subghz_protocol_renault_v1_check_remote_controller(&instance->generic);
        // Optional value
        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);
        if(!subghz_protocol_encoder_renault_v1_get_upload(instance)) {
            res = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }
        // Write back updated counter
        if(!subghz_block_write_hex(flipper_format, "Key", instance->generic.data, sizeof(uint64_t))) {
            // FURI_LOG_E(TAG, "EUUK"); //Encoder Deserialize: unable to update Key
            break;
        }
        if(!subghz_block_write_hex(
               flipper_format, "Key_2", instance->generic.data_2, sizeof(uint64_t))) {
            // FURI_LOG_E(TAG, "EUUK2"); //Encoder Deserialize: unable to update Key_2
            break;
        }
        /* Persist the advanced counter: get_upload folded the new counter into generic.seed, so write it
         * back too (only meaningful when recovered). This is what makes the next press roll forward.
         * status==1 only -- status 2 (BF not found) has no seed to persist. */
        if(instance->generic.status == 1 &&
           !subghz_block_write_hex(
               flipper_format, "Seed", instance->generic.seed, sizeof(uint32_t))) {
            break;
        }

        instance->encoder.is_running = true;

        res = SubGhzProtocolStatusOk;
    } while(false);

    return res;
}

static const SubGhzProtocolEncoder enc_renault_v1_vt = {
    .alloc = subghz_protocol_encoder_renault_v1_alloc,
    .free = subghz_protocol_encoder_generic_free,
    .deserialize = subghz_protocol_encoder_renault_v1_deserialize,
    .stop = subghz_protocol_encoder_generic_stop,
    .yield = subghz_protocol_encoder_generic_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_renault_v1 = {
    .name = SUBGHZ_PROTOCOL_RENAULT_V1_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 | SubGhzProtocolFlag_315 |
            SubGhzProtocolFlag_FM | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send |
            SubGhzProtocolFlag_Bruteforce,
    .decoder = NULL,
    .encoder = &enc_renault_v1_vt,
};

static const SubGhzEncodePluginEntry enc_renault_v1_entry = {.name = SUBGHZ_PROTOCOL_RENAULT_V1_NAME, .encoder = &enc_renault_v1_vt, .host_slot = &enc_renault_v1_host_raw};
static const FlipperAppPluginDescriptor enc_renault_v1_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_renault_v1_entry};
const FlipperAppPluginDescriptor* enc_renault_v1_ep(void) { return &enc_renault_v1_descriptor; }
