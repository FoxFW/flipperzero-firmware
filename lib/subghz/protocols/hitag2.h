#pragma once

#include <lib/subghz/protocols/base.h>

#define SUBGHZ_PROTOCOL_HITAG2_NAME "Hitag2"

typedef struct SubGhzProtocolDecoderHitag2 SubGhzProtocolDecoderHitag2;
typedef struct SubGhzProtocolEncoderHitag2 SubGhzProtocolEncoderHitag2;

extern const SubGhzProtocolDecoder subghz_protocol_hitag2_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_hitag2_encoder;
extern const SubGhzProtocol subghz_protocol_hitag2;

void* subghz_protocol_decoder_hitag2_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_hitag2_free(void* context);
void subghz_protocol_decoder_hitag2_reset(void* context);
void subghz_protocol_decoder_hitag2_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_hitag2_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_hitag2_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_hitag2_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_hitag2_get_string(void* context, FuriString* output);
