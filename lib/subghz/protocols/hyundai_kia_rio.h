#pragma once

#include <furi.h>
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/types.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <flipper_format/flipper_format.h>

#define HYUNDAI_KIA_RIO_PROTOCOL_NAME "Hyundai/Kia RIO"

typedef struct SubGhzProtocolDecoderHyundaiKiaRio SubGhzProtocolDecoderHyundaiKiaRio;
typedef struct SubGhzProtocolEncoderHyundaiKiaRio SubGhzProtocolEncoderHyundaiKiaRio;

extern const SubGhzProtocol subghz_protocol_hyundai_kia_rio;

void* subghz_protocol_decoder_hyundai_kia_rio_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_hyundai_kia_rio_free(void* context);
void subghz_protocol_decoder_hyundai_kia_rio_reset(void* context);
void subghz_protocol_decoder_hyundai_kia_rio_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_hyundai_kia_rio_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_hyundai_kia_rio_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_hyundai_kia_rio_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_hyundai_kia_rio_get_string(void* context, FuriString* output);

#ifdef ENABLE_EMULATE_FEATURE
void* subghz_protocol_encoder_hyundai_kia_rio_alloc(SubGhzEnvironment* environment);
SubGhzProtocolStatus
    subghz_protocol_encoder_hyundai_kia_rio_deserialize(void* context, FlipperFormat* flipper_format);
#endif
