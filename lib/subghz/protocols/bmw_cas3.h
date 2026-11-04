#pragma once

#include "base.h"
#include <flipper_format/flipper_format.h>

#define BMW_CAS3_PROTOCOL_NAME "BMW CAS3"

#define BMW_CAS3_MARK_US     250u
#define BMW_CAS3_SPACE_0_US  500u
#define BMW_CAS3_SPACE_1_US  1500u
#define BMW_CAS3_SYNC_HIGH_US 25000u
#define BMW_CAS3_SYNC_LOW_US 20000u
#define BMW_CAS3_TAIL_HIGH_US 75000u
#define BMW_CAS3_TAIL_LOW_US 30000u
#define BMW_CAS3_MIN_BITS    64u
#define BMW_CAS3_MAX_BITS    128u

typedef struct SubGhzProtocolDecoderBmwCas3 SubGhzProtocolDecoderBmwCas3;
typedef struct SubGhzProtocolEncoderBmwCas3 SubGhzProtocolEncoderBmwCas3;

extern const SubGhzProtocol subghz_protocol_bmw_cas3;

void* subghz_protocol_decoder_bmw_cas3_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_bmw_cas3_reset(void* context);
void subghz_protocol_decoder_bmw_cas3_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_bmw_cas3_get_hash_data(void* context);
SubGhzProtocolStatus
    subghz_protocol_decoder_bmw_cas3_deserialize(void* context, FlipperFormat* flipper_format);

void* subghz_protocol_encoder_bmw_cas3_alloc(SubGhzEnvironment* environment);
SubGhzProtocolStatus
    subghz_protocol_encoder_bmw_cas3_deserialize(void* context, FlipperFormat* flipper_format);
