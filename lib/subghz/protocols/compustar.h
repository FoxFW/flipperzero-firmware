#pragma once

#include <lib/subghz/protocols/base.h>

#define COMPUSTAR_PROTOCOL_NAME "Compustar"

typedef struct SubGhzProtocolDecoderCompustar SubGhzProtocolDecoderCompustar;
typedef struct SubGhzProtocolEncoderCompustar SubGhzProtocolEncoderCompustar;

extern const SubGhzProtocolDecoder subghz_protocol_compustar_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_compustar_encoder;
extern const SubGhzProtocol subghz_protocol_compustar;

void* subghz_protocol_encoder_compustar_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_compustar_free(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_compustar_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_encoder_compustar_stop(void* context);
LevelDuration subghz_protocol_encoder_compustar_yield(void* context);

void* subghz_protocol_decoder_compustar_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_compustar_free(void* context);
void subghz_protocol_decoder_compustar_reset(void* context);
void subghz_protocol_decoder_compustar_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_compustar_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_compustar_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_compustar_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_compustar_get_string(void* context, FuriString* output);
