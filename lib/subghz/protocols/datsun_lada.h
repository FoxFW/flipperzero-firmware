#pragma once
#include "base.h"

#define SUBGHZ_PROTOCOL_DATSUN_LADA_NAME "Datsun Lada"

typedef struct SubGhzProtocolDecoderDatsunLada SubGhzProtocolDecoderDatsunLada;

extern const SubGhzProtocolDecoder subghz_protocol_datsun_lada_decoder;
extern const SubGhzProtocol subghz_protocol_datsun_lada;

void* subghz_protocol_decoder_datsun_lada_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_datsun_lada_free(void* context);
void subghz_protocol_decoder_datsun_lada_reset(void* context);
void subghz_protocol_decoder_datsun_lada_feed(void* context, bool level, uint32_t duration);
SubGhzProtocolStatus subghz_protocol_decoder_datsun_lada_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_datsun_lada_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_datsun_lada_get_string(void* context, FuriString* output);
