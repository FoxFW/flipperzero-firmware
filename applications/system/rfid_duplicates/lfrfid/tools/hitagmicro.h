#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LFRFID_HITAGMICRO_BLOCK_SIZE 4

typedef enum {
    HitagMicroVariant8265,
    HitagMicroVariant8210,
    HitagMicroVariantH55,

    HitagMicroVariantCount,
} HitagMicroVariant;

typedef struct {
    uint8_t block0[LFRFID_HITAGMICRO_BLOCK_SIZE];
    uint8_t block1[LFRFID_HITAGMICRO_BLOCK_SIZE];
    uint8_t config[LFRFID_HITAGMICRO_BLOCK_SIZE];
} LFRFIDHitagMicro;

const uint8_t* hitagmicro_variant_password(HitagMicroVariant variant);

const char* hitagmicro_variant_name(HitagMicroVariant variant);

void hitagmicro_write(const LFRFIDHitagMicro* data, const uint8_t* password);

#ifdef __cplusplus
}
#endif
