#pragma once

#include <lib/subghz/protocols/base.h>

#define TESLA_CHARGE_PROTOCOL_NAME "Tesla Charge"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SubGhzProtocolDecoderTeslaCharge SubGhzProtocolDecoderTeslaCharge;
typedef struct SubGhzProtocolEncoderTeslaCharge SubGhzProtocolEncoderTeslaCharge;

extern const SubGhzProtocolDecoder subghz_protocol_tesla_charge_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_tesla_charge_encoder;
extern const SubGhzProtocol subghz_protocol_tesla_charge;

void* subghz_protocol_encoder_tesla_charge_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_tesla_charge_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_tesla_charge_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_tesla_charge_stop(void* context);

LevelDuration subghz_protocol_encoder_tesla_charge_yield(void* context);

void* subghz_protocol_decoder_tesla_charge_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_tesla_charge_free(void* context);

void subghz_protocol_decoder_tesla_charge_reset(void* context);

void subghz_protocol_decoder_tesla_charge_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_tesla_charge_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_tesla_charge_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_tesla_charge_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_tesla_charge_get_string(void* context, FuriString* output);

#ifdef __cplusplus
}
#endif
