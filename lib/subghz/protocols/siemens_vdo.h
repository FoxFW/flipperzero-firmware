#pragma once

#include <lib/subghz/protocols/base.h>

#define SIEMENS_VDO_PROTOCOL_NAME "Siemens VDO"

typedef struct SubGhzProtocolDecoderSiemensVdo SubGhzProtocolDecoderSiemensVdo;
typedef struct SubGhzProtocolEncoderSiemensVdo SubGhzProtocolEncoderSiemensVdo;

extern const SubGhzProtocolDecoder subghz_protocol_siemens_vdo_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_siemens_vdo_encoder;
extern const SubGhzProtocol subghz_protocol_siemens_vdo;

void* subghz_protocol_decoder_siemens_vdo_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_siemens_vdo_free(void* context);
void subghz_protocol_decoder_siemens_vdo_reset(void* context);
void subghz_protocol_decoder_siemens_vdo_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_siemens_vdo_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_siemens_vdo_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_siemens_vdo_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_siemens_vdo_get_string(void* context, FuriString* output);
