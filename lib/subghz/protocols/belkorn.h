#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_BELKORN_NAME "BelKorn"

typedef struct SubGhzProtocolDecoderBelKorn SubGhzProtocolDecoderBelKorn;
typedef struct SubGhzProtocolEncoderBelKorn SubGhzProtocolEncoderBelKorn;

extern const SubGhzProtocolDecoder subghz_protocol_belkorn_decoder;
extern const SubGhzProtocol subghz_protocol_belkorn;

void* subghz_protocol_decoder_belkorn_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_belkorn_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_belkorn_get_hash_data(void* context);

SubGhzProtocolStatus
    subghz_protocol_decoder_belkorn_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_belkorn_get_string(void* context, FuriString* output);
