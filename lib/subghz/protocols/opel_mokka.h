#pragma once
#include "base.h"

#define SUBGHZ_PROTOCOL_OPEL_MOKKA_NAME "Opel-Mokka"

typedef struct SubGhzProtocolDecoderOpelMokka SubGhzProtocolDecoderOpelMokka;

extern const SubGhzProtocolDecoder subghz_protocol_opel_mokka_decoder;
extern const SubGhzProtocol subghz_protocol_opel_mokka;

void* subghz_protocol_decoder_opel_mokka_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_opel_mokka_free(void* context);
void subghz_protocol_decoder_opel_mokka_reset(void* context);
void subghz_protocol_decoder_opel_mokka_feed(void* context, bool level, uint32_t duration);
SubGhzProtocolStatus subghz_protocol_decoder_opel_mokka_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_opel_mokka_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_opel_mokka_get_string(void* context, FuriString* output);
