#pragma once

#include <lib/subghz/protocols/base.h>

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include "tpms_generic.h"
#include <lib/subghz/blocks/math.h>

#define TPMS_PROTOCOL_ABARTH_124_NAME "Abarth 124"

typedef struct TPMSProtocolDecoderAbarth124 TPMSProtocolDecoderAbarth124;
typedef struct TPMSProtocolEncoderAbarth124 TPMSProtocolEncoderAbarth124;

extern const SubGhzProtocolDecoder tpms_protocol_abarth_124_decoder;
extern const SubGhzProtocolEncoder tpms_protocol_abarth_124_encoder;
extern const SubGhzProtocol tpms_protocol_abarth_124;

void* tpms_protocol_decoder_abarth_124_alloc(SubGhzEnvironment* environment);

void tpms_protocol_decoder_abarth_124_free(void* context);

void tpms_protocol_decoder_abarth_124_reset(void* context);

void tpms_protocol_decoder_abarth_124_feed(void* context, bool level, uint32_t duration);

uint8_t tpms_protocol_decoder_abarth_124_get_hash_data(void* context);

SubGhzProtocolStatus tpms_protocol_decoder_abarth_124_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    tpms_protocol_decoder_abarth_124_deserialize(void* context, FlipperFormat* flipper_format);

void tpms_protocol_decoder_abarth_124_get_string(void* context, FuriString* output);
