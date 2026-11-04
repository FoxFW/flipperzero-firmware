#pragma once

#include <lib/subghz/protocols/base.h>

#define SUBGHZ_PROTOCOL_FIAT_NAME "Fiat Multi"

extern const SubGhzProtocolDecoder subghz_protocol_fiat_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_fiat_encoder;
extern const SubGhzProtocol subghz_protocol_fiat_v2_v3_v5;

/* Decoder */
void* subghz_protocol_decoder_fiat_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_fiat_free(void* context);
void subghz_protocol_decoder_fiat_reset(void* context);
void subghz_protocol_decoder_fiat_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_fiat_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_fiat_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_fiat_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_fiat_get_string(void* context, FuriString* output);

/* Encoder */
void* subghz_protocol_encoder_fiat_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_fiat_free(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_fiat_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_encoder_fiat_stop(void* context);
LevelDuration subghz_protocol_encoder_fiat_yield(void* context);
