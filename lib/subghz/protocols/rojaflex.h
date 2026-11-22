#pragma once
#include "base.h"

#define SUBGHZ_PROTOCOL_ROJAFLEX_NAME "RojaFlex"

typedef struct SubGhzProtocolDecoderRojaflex SubGhzProtocolDecoderRojaflex;

extern const SubGhzProtocolDecoder subghz_protocol_rojaflex_decoder;
extern const SubGhzProtocol subghz_protocol_rojaflex;

void* subghz_protocol_decoder_rojaflex_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_rojaflex_free(void* context);
void subghz_protocol_decoder_rojaflex_reset(void* context);
void subghz_protocol_decoder_rojaflex_feed(void* context, bool level, uint32_t duration);
SubGhzProtocolStatus subghz_protocol_decoder_rojaflex_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_rojaflex_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_rojaflex_get_string(void* context, FuriString* output);
