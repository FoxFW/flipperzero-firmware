#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_HORMANN_BISECUR_NAME "Hormann BiSecur"

typedef struct SubGhzProtocolDecoderHormannBiSecur SubGhzProtocolDecoderHormannBiSecur;

extern const SubGhzProtocolDecoder subghz_protocol_hormann_bisecur_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_hormann_bisecur_encoder;
extern const SubGhzProtocol subghz_protocol_hormann_bisecur;

void* subghz_protocol_decoder_hormann_bisecur_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_hormann_bisecur_free(void* context);

void subghz_protocol_decoder_hormann_bisecur_reset(void* context);

void subghz_protocol_decoder_hormann_bisecur_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_hormann_bisecur_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_hormann_bisecur_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus subghz_protocol_decoder_hormann_bisecur_deserialize(
    void* context,
    FlipperFormat* flipper_format);

void subghz_protocol_decoder_hormann_bisecur_get_string(void* context, FuriString* output);
