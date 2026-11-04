#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <lib/toolbox/level_duration.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool is_running;
    size_t repeat;
    size_t front;
    size_t size_upload;
    LevelDuration* upload;

} SubGhzProtocolBlockEncoder;

typedef enum {
    SubGhzProtocolBlockAlignBitLeft,
    SubGhzProtocolBlockAlignBitRight,
} SubGhzProtocolBlockAlignBit;

void subghz_protocol_blocks_set_bit_array(
    bool bit_value,
    uint8_t data_array[],
    size_t set_index_bit,
    size_t max_size_array);

bool subghz_protocol_blocks_get_bit_array(uint8_t data_array[], size_t read_index_bit);

size_t subghz_protocol_blocks_get_upload_from_bit_array(
    uint8_t data_array[],
    size_t count_bit_data_array,
    LevelDuration* upload,
    size_t max_size_upload,
    uint32_t duration_bit,
    SubGhzProtocolBlockAlignBit align_bit);

#define SUBGHZ_ASSERT_ENCODER_PREFIX(type) \
    _Static_assert(offsetof(type, base) == 0, #type " must have base as first member")

void subghz_protocol_encoder_generic_free(void* context);
void subghz_protocol_encoder_generic_stop(void* context);
LevelDuration subghz_protocol_encoder_generic_yield(void* context);

#ifdef __cplusplus
}
#endif
