#pragma once

#include "types.h"
#include "environment.h"
#include "protocols/base.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SubGhzTransmitter SubGhzTransmitter;

SubGhzTransmitter*
    subghz_transmitter_alloc_init(SubGhzEnvironment* environment, const char* protocol_name);

void subghz_transmitter_free(SubGhzTransmitter* instance);

SubGhzProtocolEncoderBase* subghz_transmitter_get_protocol_instance(SubGhzTransmitter* instance);

uint8_t subghz_transmitter_get_raw_progress(SubGhzTransmitter* instance);

bool subghz_transmitter_stop(SubGhzTransmitter* instance);

SubGhzProtocolStatus
    subghz_transmitter_deserialize(SubGhzTransmitter* instance, FlipperFormat* flipper_format);

LevelDuration subghz_transmitter_yield(void* context);

#ifdef __cplusplus
}
#endif
