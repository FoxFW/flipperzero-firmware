#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_TELECRANE_NAME "Telecrane"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SubGhzProtocolDecoderTelecrane SubGhzProtocolDecoderTelecrane;
typedef struct SubGhzProtocolEncoderTelecrane SubGhzProtocolEncoderTelecrane;

extern const SubGhzProtocolDecoder subghz_protocol_telecrane_decoder;
extern const SubGhzProtocol subghz_protocol_telecrane;

void* subghz_protocol_decoder_telecrane_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_telecrane_feed(void* context, bool level, uint32_t duration);
void subghz_protocol_decoder_telecrane_reset(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_telecrane_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_telecrane_deserialize(void* context, FlipperFormat* flipper_format);

#ifdef __cplusplus
}
#endif
