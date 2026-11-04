#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_CARDIN_S508_NAME "Cardin S508"

typedef struct SubGhzProtocolDecoderCardinS508 SubGhzProtocolDecoderCardinS508;
typedef struct SubGhzProtocolEncoderCardinS508 SubGhzProtocolEncoderCardinS508;

extern const SubGhzProtocolDecoder subghz_protocol_cardin_s508_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_cardin_s508_encoder;
extern const SubGhzProtocol subghz_protocol_cardin_s508;

void* subghz_protocol_decoder_cardin_s508_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_cardin_s508_free(void* context);

void subghz_protocol_decoder_cardin_s508_reset(void* context);

void subghz_protocol_decoder_cardin_s508_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_cardin_s508_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_cardin_s508_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_cardin_s508_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_cardin_s508_get_string(void* context, FuriString* output);
