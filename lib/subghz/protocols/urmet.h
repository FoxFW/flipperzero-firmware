#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_URMET_NAME "Urmet"

typedef struct SubGhzProtocolDecoderUrmet SubGhzProtocolDecoderUrmet;
typedef struct SubGhzProtocolEncoderUrmet SubGhzProtocolEncoderUrmet;

extern const SubGhzProtocolDecoder subghz_protocol_urmet_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_urmet_encoder;
extern const SubGhzProtocol subghz_protocol_urmet;

void* subghz_protocol_encoder_urmet_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_urmet_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_urmet_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_urmet_stop(void* context);

LevelDuration subghz_protocol_encoder_urmet_yield(void* context);

void* subghz_protocol_decoder_urmet_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_urmet_free(void* context);

void subghz_protocol_decoder_urmet_reset(void* context);

void subghz_protocol_decoder_urmet_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_urmet_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_urmet_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_urmet_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_urmet_get_string(void* context, FuriString* output);
