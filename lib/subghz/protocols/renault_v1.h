#pragma once

#include "base.h"
#include "renault_v1_crypto.h"

#define SUBGHZ_PROTOCOL_RENAULT_V1_NAME "Hitag-2"

typedef struct SubGhzProtocolDecoderRenaultV1 SubGhzProtocolDecoderRenaultV1;
typedef struct SubGhzProtocolEncoderRenaultV1 SubGhzProtocolEncoderRenaultV1;

extern const SubGhzProtocolDecoder subghz_protocol_renault_v1_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_renault_v1_encoder;
extern const SubGhzProtocol subghz_protocol_renault_v1;

void* subghz_protocol_encoder_renault_v1_alloc(SubGhzEnvironment* environment);

SubGhzProtocolStatus
    subghz_protocol_encoder_renault_v1_deserialize(void* context, FlipperFormat* flipper_format);

void* subghz_protocol_decoder_renault_v1_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_renault_v1_reset(void* context);

void subghz_protocol_decoder_renault_v1_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_renault_v1_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_renault_v1_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_renault_v1_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_renault_v1_get_string(void* context, FuriString* output);

bool subghz_protocol_renault_v1_needs_bruteforce(void* decoder_context);

void subghz_protocol_renault_v1_bf_init(RenaultV1BfContext* ctx, void* decoder_context);

void subghz_protocol_renault_v1_bf_apply(RenaultV1BfContext* ctx, void* decoder_context);

void subghz_protocol_renault_v1_bf_set_not_found(void* decoder_context);

uint32_t renault_v1_iv_to_u32(const uint8_t iv[4]);
