#pragma once
#include "base.h"

#define SUBGHZ_PROTOCOL_HCS362_NAME "Microchip-HCS362"

typedef struct SubGhzProtocolDecoderHCS362 SubGhzProtocolDecoderHCS362;

extern const SubGhzProtocolDecoder subghz_protocol_hcs362_decoder;
extern const SubGhzProtocol subghz_protocol_hcs362;

void* subghz_protocol_decoder_hcs362_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_hcs362_free(void* context);
void subghz_protocol_decoder_hcs362_reset(void* context);
void subghz_protocol_decoder_hcs362_feed(void* context, bool level, uint32_t duration);
SubGhzProtocolStatus subghz_protocol_decoder_hcs362_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_hcs362_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_hcs362_get_string(void* context, FuriString* output);
