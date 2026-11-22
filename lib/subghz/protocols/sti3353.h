#pragma once
#include "base.h"

#define SUBGHZ_PROTOCOL_STI3353_NAME "STI-3353"

typedef struct SubGhzProtocolDecoderSTI3353 SubGhzProtocolDecoderSTI3353;

extern const SubGhzProtocolDecoder subghz_protocol_sti3353_decoder;
extern const SubGhzProtocol subghz_protocol_sti3353;

void* subghz_protocol_decoder_sti3353_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_sti3353_free(void* context);
void subghz_protocol_decoder_sti3353_reset(void* context);
void subghz_protocol_decoder_sti3353_feed(void* context, bool level, uint32_t duration);
SubGhzProtocolStatus subghz_protocol_decoder_sti3353_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus subghz_protocol_decoder_sti3353_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_sti3353_get_string(void* context, FuriString* output);
