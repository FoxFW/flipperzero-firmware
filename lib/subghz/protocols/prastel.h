#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_PRASTEL_NAME "Prastel"

#define SUBGHZ_PROTOCOL_PRASTEL_SERIAL_MASK (0x001FFFFFUL)

typedef struct SubGhzProtocolDecoderPrastel SubGhzProtocolDecoderPrastel;
typedef struct SubGhzProtocolEncoderPrastel SubGhzProtocolEncoderPrastel;

extern const SubGhzProtocolDecoder subghz_protocol_prastel_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_prastel_encoder;
extern const SubGhzProtocol subghz_protocol_prastel;

void* subghz_protocol_decoder_prastel_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_prastel_feed(void* context, bool level, uint32_t duration);

SubGhzProtocolStatus
    subghz_protocol_decoder_prastel_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_prastel_get_string(void* context, FuriString* output);
