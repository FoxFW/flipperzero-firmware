#pragma once

#include <lib/subghz/protocols/base.h>

#define SUBGHZ_PROTOCOL_HONDA_V2_V3_NAME "HondaV2&V3"

extern const SubGhzProtocolDecoder subghz_protocol_honda_v2_v3_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_honda_v2_v3_encoder;
extern const SubGhzProtocol subghz_protocol_honda_v2_v3;

/* Decoder */
void* subghz_protocol_decoder_honda_v2_v3_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_honda_v2_v3_free(void* context);
void subghz_protocol_decoder_honda_v2_v3_reset(void* context);
void subghz_protocol_decoder_honda_v2_v3_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_honda_v2_v3_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_honda_v2_v3_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_honda_v2_v3_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_honda_v2_v3_get_string(void* context, FuriString* output);

/* Encoder */
void* subghz_protocol_encoder_honda_v2_v3_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_honda_v2_v3_free(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_honda_v2_v3_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_encoder_honda_v2_v3_stop(void* context);
LevelDuration subghz_protocol_encoder_honda_v2_v3_yield(void* context);
