#pragma once

#include <stdint.h>
#include <stdbool.h>

#define RENAULT_V1_BF_RANGE 0x40000

typedef enum {
    RenaultV1BfStateIdle = 0,
    RenaultV1BfStateRunning,
    RenaultV1BfStateFound,
    RenaultV1BfStateNotFound,
    RenaultV1BfStateCancelled,
} RenaultV1BfState;

typedef struct {

    volatile bool cancel_requested;
    volatile uint32_t progress;
    volatile uint8_t state;
    uint32_t total;

    uint8_t payload[11];

    uint8_t iv[4];
} RenaultV1BfContext;

void subghz_protocol_renault_v1_bf_run(RenaultV1BfContext* ctx);

void subghz_protocol_renault_v1_encrypt(
    const uint8_t serial[4],
    const uint8_t iv_plain[4],
    uint8_t out[11]);
