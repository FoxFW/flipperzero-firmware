#pragma once

#include <lib/subghz/protocols/base.h>

#define GM_RKE_PROTOCOL_NAME "GM ABO1502T"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SubGhzProtocolDecoderGmRke SubGhzProtocolDecoderGmRke;
typedef struct SubGhzProtocolEncoderGmRke SubGhzProtocolEncoderGmRke;

extern const SubGhzProtocolDecoder subghz_protocol_gm_rke_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_gm_rke_encoder;
extern const SubGhzProtocol subghz_protocol_gm_rke;

void* subghz_protocol_encoder_gm_rke_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_gm_rke_free(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_gm_rke_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_encoder_gm_rke_stop(void* context);
LevelDuration subghz_protocol_encoder_gm_rke_yield(void* context);

void* subghz_protocol_decoder_gm_rke_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_gm_rke_free(void* context);
void subghz_protocol_decoder_gm_rke_reset(void* context);
void subghz_protocol_decoder_gm_rke_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_gm_rke_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_gm_rke_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_gm_rke_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_gm_rke_get_string(void* context, FuriString* output);

#ifdef __cplusplus
}
#endif
