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

#define FIAT_V2_PROTOCOL_NAME "Fiat V2"

typedef struct SubGhzProtocolDecoderFiatV2 SubGhzProtocolDecoderFiatV2;

extern const SubGhzProtocol subghz_protocol_fiat_v2;

void* subghz_protocol_decoder_fiat_v2_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_fiat_v2_reset(void* context);
void subghz_protocol_decoder_fiat_v2_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_fiat_v2_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_fiat_v2_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_fiat_v2_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_fiat_v2_get_string(void* context, FuriString* output);

void* subghz_protocol_encoder_fiat_v2_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_fiat_v2_free(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_fiat_v2_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_encoder_fiat_v2_stop(void* context);
LevelDuration subghz_protocol_encoder_fiat_v2_yield(void* context);

#define FIAT_V2_IV_COMBO_COUNT 4U

uint8_t subghz_protocol_fiat_v2_iv_button_for_combo(const uint8_t* raw, uint8_t combo);
uint16_t subghz_protocol_fiat_v2_iv_control_for_combo(const uint8_t* raw, uint8_t combo);
