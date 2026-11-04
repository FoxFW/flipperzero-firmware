#pragma once

#include <furi.h>
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/types.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <flipper_format/flipper_format.h>

#define FIAT_V1_PROTOCOL_NAME "Fiat V1"
#define FIAT_V1_HITAG2_KEY_FIELD    "Hitag2 Key"
#define FIAT_V1_HITAG2_EPOCH_FIELD  "Hitag2 Epoch"

typedef struct SubGhzProtocolDecoderFiatV1 SubGhzProtocolDecoderFiatV1;
typedef struct SubGhzProtocolEncoderFiatV1 SubGhzProtocolEncoderFiatV1;

extern const SubGhzProtocol fiat_v1_protocol;

void* subghz_protocol_decoder_fiat_v1_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_fiat_v1_reset(void* context);
void subghz_protocol_decoder_fiat_v1_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_fiat_v1_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_fiat_v1_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_fiat_v1_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_fiat_v1_get_string(void* context, FuriString* output);

uint32_t fiat_v1_hitag2_recovery_total(void);
uint32_t fiat_v1_hitag2_recovery_total_range(uint8_t key_start, uint8_t key_end);

typedef bool (
    *FiatV1RecoverProgressCallback)(uint8_t progress, uint32_t combos_tested, void* context);

bool fiat_v1_hitag2_recover(
    uint32_t uid,
    uint8_t button,
    uint16_t control,
    uint32_t hop,
    uint8_t out_key[6],
    uint32_t* out_epoch,
    FiatV1RecoverProgressCallback progress_cb,
    void* progress_ctx);

bool fiat_v1_hitag2_recover_range(
    uint32_t uid,
    uint8_t button,
    uint16_t control,
    uint32_t hop,
    uint8_t key_start,
    uint8_t key_end,
    uint8_t out_key[6],
    uint32_t* out_epoch,
    FiatV1RecoverProgressCallback progress_cb,
    void* progress_ctx);

void* subghz_protocol_encoder_fiat_v1_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_fiat_v1_free(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_fiat_v1_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_encoder_fiat_v1_stop(void* context);
LevelDuration subghz_protocol_encoder_fiat_v1_yield(void* context);

#define FIAT_V1_KNOWN_KEY_COUNT 95U
// L1 = the first 8 high-confidence keys (fast full-epoch sweep). L2 = the remaining
// manufacturer/factory keys [8,95), swept on OK or over the ESP32 companion.
#define FIAT_V1_DEEP_SWEEP_KEY_COUNT 8U

uint32_t subghz_protocol_fiat_v1_compute_auth(
    uint32_t uid,
    uint8_t button,
    uint16_t control,
    const uint8_t key[6],
    uint32_t epoch);

bool subghz_protocol_fiat_v1_verify_key(
    uint32_t uid,
    uint8_t button,
    uint16_t control,
    uint32_t hop,
    const uint8_t key[6],
    uint32_t epoch);

const uint8_t (*subghz_protocol_fiat_v1_get_known_keys(void))[6];
