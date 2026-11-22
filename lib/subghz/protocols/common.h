
#pragma once

#include <stddef.h>

#include "base.h"

#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
} SubGhzProtocolEncoderCommon;

typedef struct {
    SubGhzProtocolEncoderCommon common;
    SubGhzBlockGeneric generic;
} SubGhzProtocolEncoderCommonGeneric;

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
} SubGhzProtocolDecoderCommon;

typedef struct {
    SubGhzProtocolDecoderCommon common;
    uint32_t te;
} SubGhzProtocolDecoderCommonTe;

#define SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(type)                                        \
    _Static_assert(                                                                      \
        offsetof(type, base) == offsetof(SubGhzProtocolDecoderCommon, base) &&           \
            offsetof(type, decoder) == offsetof(SubGhzProtocolDecoderCommon, decoder) && \
            offsetof(type, generic) == offsetof(SubGhzProtocolDecoderCommon, generic),   \
        #type " must start with the SubGhzProtocolDecoderCommon member sequence")

#define SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(type)                                      \
    _Static_assert(                                                                    \
        offsetof(type, base) == offsetof(SubGhzProtocolEncoderCommon, base) &&         \
            offsetof(type, encoder) == offsetof(SubGhzProtocolEncoderCommon, encoder), \
        #type " must start with the SubGhzProtocolEncoderCommon member sequence")

#define SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(type)                                        \
    SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(type);                                            \
    _Static_assert(                                                                       \
        offsetof(type, generic) == offsetof(SubGhzProtocolEncoderCommonGeneric, generic), \
        #type " must keep generic directly after encoder")

#define SUBGHZ_ASSERT_DECODER_TE_LAYOUT(type)                              \
    SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(type);                             \
    _Static_assert(                                                        \
        offsetof(type, te) == offsetof(SubGhzProtocolDecoderCommonTe, te), \
        #type " must keep te directly after generic")

void* subghz_protocol_decoder_common_alloc(size_t instance_size, const SubGhzProtocol* protocol);

void* subghz_protocol_encoder_common_alloc(
    size_t instance_size,
    const SubGhzProtocol* protocol,
    size_t repeat,
    size_t size_upload);

typedef bool (*SubGhzProtocolEncoderGetUpload)(void* instance);

SubGhzProtocolStatus subghz_protocol_encoder_common_deserialize(
    void* context,
    FlipperFormat* flipper_format,
    uint16_t min_count_bit,
    SubGhzProtocolEncoderGetUpload get_upload);

SubGhzProtocolStatus subghz_protocol_decoder_common_deserialize_te(
    void* context,
    FlipperFormat* flipper_format,
    uint16_t min_count_bit);

void subghz_protocol_encoder_common_free(void* context);

void subghz_protocol_encoder_common_stop(void* context);

LevelDuration subghz_protocol_encoder_common_yield(void* context);

void subghz_protocol_decoder_common_free(void* context);

void subghz_protocol_decoder_common_reset(void* context);

uint8_t subghz_protocol_decoder_common_get_hash_data(void* context);

uint8_t subghz_protocol_decoder_common_get_hash_data_serial(void* context);

void subghz_protocol_decoder_common_get_string(void* context, FuriString* output);

SubGhzProtocolStatus subghz_protocol_decoder_common_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus subghz_protocol_common_append_data_2(
    SubGhzProtocolStatus status,
    SubGhzBlockGeneric* generic,
    FlipperFormat* flipper_format);

SubGhzProtocolStatus subghz_protocol_decoder_common_serialize_data_2(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus subghz_protocol_decoder_common_serialize_te(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

#ifdef __cplusplus
}
#endif
