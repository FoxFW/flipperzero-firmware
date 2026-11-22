#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_KEYFINDER2_NAME "KeyFinder2"

typedef struct SubGhzProtocolDecoderKeyFinder2 SubGhzProtocolDecoderKeyFinder2;
typedef struct SubGhzProtocolEncoderKeyFinder2 SubGhzProtocolEncoderKeyFinder2;

extern const SubGhzProtocolDecoder subghz_protocol_keyfinder2_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_keyfinder2_encoder;
extern const SubGhzProtocol subghz_protocol_keyfinder2;

void* subghz_protocol_encoder_keyfinder2_alloc(SubGhzEnvironment* environment);

SubGhzProtocolStatus
    subghz_protocol_encoder_keyfinder2_deserialize(void* context, FlipperFormat* flipper_format);

void* subghz_protocol_decoder_keyfinder2_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_keyfinder2_feed(void* context, bool level, uint32_t duration);

SubGhzProtocolStatus
    subghz_protocol_decoder_keyfinder2_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_keyfinder2_get_string(void* context, FuriString* output);
