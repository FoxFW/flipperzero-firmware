#pragma once
#include "base.h"

#define SUBGHZ_PROTOCOL_HCS361_NAME "Microchip-HCS361"

typedef struct SubGhzProtocolDecoderHCS361 SubGhzProtocolDecoderHCS361;

extern const SubGhzProtocolDecoder subghz_protocol_hcs361_decoder;
extern const SubGhzProtocol subghz_protocol_hcs361;

void* subghz_protocol_decoder_hcs361_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_hcs361_free(void* context);
void subghz_protocol_decoder_hcs361_reset(void* context);
void subghz_protocol_decoder_hcs361_feed(void* context, bool level, uint32_t duration);
SubGhzProtocolStatus subghz_protocol_decoder_hcs361_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_hcs361_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_hcs361_get_string(void* context, FuriString* output);
