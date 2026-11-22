#pragma once

#include <lib/subghz/protocols/base.h>

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include "tpms_generic.h"
#include <lib/subghz/blocks/math.h>

#define TPMS_PROTOCOL_SCHRADER_EG53MA4_NAME "Schrader EG53MA4"

typedef struct TPMSProtocolDecoderSchraderEG53MA4 TPMSProtocolDecoderSchraderEG53MA4;
typedef struct TPMSProtocolEncoderSchraderEG53MA4 TPMSProtocolEncoderSchraderEG53MA4;

extern const SubGhzProtocolDecoder tpms_protocol_schrader_eg53ma4_decoder;
extern const SubGhzProtocolEncoder tpms_protocol_schrader_eg53ma4_encoder;
extern const SubGhzProtocol tpms_protocol_schrader_eg53ma4;

void* tpms_protocol_decoder_schrader_eg53ma4_alloc(SubGhzEnvironment* environment);

void tpms_protocol_decoder_schrader_eg53ma4_free(void* context);

void tpms_protocol_decoder_schrader_eg53ma4_reset(void* context);

void tpms_protocol_decoder_schrader_eg53ma4_feed(void* context, bool level, uint32_t duration);

uint8_t tpms_protocol_decoder_schrader_eg53ma4_get_hash_data(void* context);

SubGhzProtocolStatus tpms_protocol_decoder_schrader_eg53ma4_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus tpms_protocol_decoder_schrader_eg53ma4_deserialize(
    void* context,
    FlipperFormat* flipper_format);

void tpms_protocol_decoder_schrader_eg53ma4_get_string(void* context, FuriString* output);
