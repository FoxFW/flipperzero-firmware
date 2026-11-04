#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SubGhzBlockDecoder SubGhzBlockDecoder;

struct SubGhzBlockDecoder {
    uint32_t parser_step;
    uint32_t te_last;
    uint64_t decode_data;
    uint8_t decode_count_bit;
};

void subghz_protocol_blocks_add_bit(SubGhzBlockDecoder* decoder, uint8_t bit);

void subghz_protocol_blocks_add_to_128_bit(
    SubGhzBlockDecoder* decoder,
    uint8_t bit,
    uint64_t* head_64_bit);

uint8_t subghz_protocol_blocks_get_hash_data(SubGhzBlockDecoder* decoder, size_t len);

#define SUBGHZ_ASSERT_DECODER_PREFIX(type) \
    _Static_assert(offsetof(type, base) == 0, #type " must have base as first member")

void subghz_protocol_decoder_generic_free(void* context);

#ifdef __cplusplus
}
#endif
